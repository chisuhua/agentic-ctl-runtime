# ADR-0087: Cloud Adapter Threading Model (多线程SIGSEGV 根因修复路径)

**日期**: 2026-09-08
**父主题**: fix-cloud-adapter-multithreading Wave 1 #2 (SerializingDecorator) 后续根因修复

## 状态

🟡 Partial (Sprint 24 调研完成, §Step 1+2 假设偏差已修订, 见 §实施日志)

> **背景 (2026-09-08)**: Wave 1 #2 `fix-cloud-adapter-multithreading` 通过工厂层
> `SerializingDecorator` (mutex + cv 串行化 generate/generate_stream) 规避了
> CloudLLMAdapter 多线程 SIGSEGV — **这是规避, 不是根因修复**. 本 ADR 记录根因
> 诊断 + 升级路径, 防止 N→1 性能税意外固化为永久架构.
>
> **Sprint 24 修订 (2026-09-10)**: 见 §实施日志. 调研发现 §Step 1+2 假设
> 均部分/完全失效 (OpenSSL 已 3.0.13,代码 0 行 OpenSSL 调用; httplib 0.18.4
> 已含 PR #701 threading fix). 升级路径重定位至 httplib CVE-2026-33745
> (Critical security) 升级, 真根因待 Sprint 26 实证.
>
> **OpenSpec 追踪**: `openspec/changes/cloud-adapter-threading-root-cause/` (scaffold).
> **Wave 1 #2 实施**: ship (commit `3d653e0` + Phase 2 commits).

## 决策

### 决策 1 — 当前默认行为保持 SerializingDecorator 包装

CloudLLMAdapter 工厂创建路径默认注入 `SerializingDecorator` (mutex + cv
串行化 generate/generate_stream), 根除 N≥2 worker 并发 + Authorization header +
https 三者同时触发的 SIGSEGV. 真根因修复前**不**移除默认包装.

### 决策 2 — 根因修复路径 (后续独立 change)

实施根因修复时按以下顺序升级:
1. OpenSSL 1.1+ → 3.0 集成 (find_package + 链接 + SSL_CTX per-thread 正确初始化)
2. httplib 升级 (upstream 多线程 Authorization header fix 调研 + 集成)
3. 现有测试验证 (Phase B B.2 4 worker 真并发 + Phase E Skill IPC 多 worker +
   Phase G ContextCompactor 多 worker 真并发)
4. 移除 `LLMProviderFactory` cloud 路径默认 `SerializingDecorator` 包装
5. 性能 benchmark (4 worker 并发 deepseek: baseline 串行 ~12s vs fix 后并发 ~3s)

### 决策 3 — SerializingDecorator 保留为 OPT-IN fail-safe

升级完成后 `SerializingDecorator` **不删除**, 转为 OPT-IN 降级用途:
- `LLMProviderFactory::create(config, opts)` opts.serializer = true 时启用
- 默认 false (升级后无需串行化)
- 用途: 诊断 httplib/OpenSSL 退化场景 + 紧急降级开关

### 决策 4 — ADR 引用锚点

所有现有 `ADR-XXXX (Cloud adapter threading model)` 占位符引用替换为
`ADR-0087`. 引用位置清单 (Phase 1.2-1.6):
- `src/common/llm/serializing_decorator.h` (header doc, 2 处)
- `src/common/llm/serializing_decorator.cpp` (implementation comments, 1 处)
- `tests/test_serializing_decorator.cpp` (test comments, 1 处)
- `tests/test_cloud_adapter_multithread.cpp` (test comments, 1 处)
- `openspec/changes/fix-cloud-adapter-multithreading/proposal.md` (follow-up note, 1 处)
- `openspec/changes/fix-cloud-adapter-multithreading/design.md` (follow-up note, 1 处)

## 根因诊断 (gdb 实证, 来自 fix-cloud-adapter-multithreading proposal.md)

gdb backtrace (commit `c0cb522` Phase B ship):
```
Thread N received signal SIGSEGV
#0  std::function<void(int)>::operator()() const (this=0x11, ...)
#1  httplib::detail::create_client_socket(...)
#2  httplib::ClientImpl::send_(...)
#3  httplib::Client::Post(...)
#4  CloudLLMAdapter::do_post(...)
#5  ILLMProvider::generate(...)
```

**触发条件** (3 因素同时):
- N≥2 worker 并发调 `CloudLLMAdapter::do_post`
- 带 `Authorization: Bearer <api_key>` header (config_.api_key 非空)
- https URL (OpenSSL 路径)

**排除诊断**:
- ❌ httplib 自身: `HttpLLMAdapter` 相同路径 PASS
- ❌ 单线程本身: `pool(1)` 真实 deepseek PASS
- ❌ 多线程 SSL 本身: `MockLLMProvider` 4 worker 并发 PASS

**根因候选**:
1. **OpenSSL `SSL_CTX` 多线程初始化竞态** — httplib 或 OpenSSL 默认 SSL_CTX
   不是 per-thread, 多线程同时首次建立 SSL 连接触发 `SSL_new` /
   `SSL_CTX_use_certificate` 等内部状态 corruption. OpenSSL 1.1+ 自带
   线程安全, 但需正确链接 + 锁初始化.
2. **httplib Authorization header 栈处理 bug** — httplib 内部对某些 header
   value 的栈分配在多线程下 corruption (gdb backtrace 显示 `socket_options_`
   栈 corruption, `std::function<void(int)>` 跳到空地址 `0x11`).

## 升级路径 (实施细节)

### Step 1 — OpenSSL 3.0 集成

CMake:
```cmake
find_package(OpenSSL 3.0 REQUIRED)
target_link_libraries(agenticdsl_common PUBLIC OpenSSL::SSL OpenSSL::Crypto)
```

验证:
- SSL_CTX per-thread 正确初始化 (OpenSSL 3.0 默认 `OSSL_set_max_threads`)
- 现有 SSL 调用代码 ABI 兼容 (OpenSSL 3.0 提供 v1.1 → 3.0 兼容层)

> **Sprint 24 修订 (2026-09-10)**: §Step 1 假设**已隐式完成,0 LOC 迁移**。
> - 系统 OpenSSL 已是 3.0.13 (`cmake -LA build | grep openssl` + `openssl version`)
> - `find_package(OpenSSL REQUIRED)` 无版本约束,自动找最高版本
> - `cloud_adapter.cpp` **0 行 OpenSSL 代码** (无 SSL_CTX/EVP_/X509_ 直接调用)
> - 所有 SSL 操作封装在 `httplib::Client` 内 (line 244, 265)
>
> 详见审计: `docs/audits/2026-09-10-adr-0087-sprint-24-openssl-audit.md`
> §修订建议: Step 1 从 ADR 升级路径中**删除或重写**,Sprint 25 范围重定位。

### Step 2 — httplib 升级

调研 upstream commit history:
- 是否已修复 Authorization header 多线程栈 corruption
- API 变更 (compat with current `external/httplib/httplib.h` usage)

集成:
- 替换 `external/httplib/httplib.h` 为 upstream 最新版
- 回归测试现有 `tests/test_http_adapter.cpp` 单线程路径

> **Sprint 24 修订 (2026-09-10)**: §Step 2 假设**部分失效,需重写升级理由**。
> - **真实 threading fix 是 PR #701** (merged Nov 29, 2020, shipped in v0.8.0)
>   - Issue #697 + #699 — `SSL_shutdown on already-closed socket` race
>   - 关键代码: `socket_requests_in_flight_` (line 1507-1509), `ctx_mutex_` (8940-8968)
> - 当前 vendored **httplib 0.18.4 已含 PR #701 fix** (本地 grep 实证)
> - **handoff 提到的 issue #1903 与 threading 无关** (是 Windows file size 32-bit bug)
> - ADR §根因诊断 (line 82-84) 混淆 PR #701 (threading) vs #1903 (Windows file size)
>
> **真正升级理由 — CVE-2026-33745 (GHSA-6hrp-7fq9-3qv2)**:
> - Severity: **Critical** (auth credentials leaked on cross-origin redirect)
> - Fixed in: **v0.39.0**
> - 当前 0.18.4 未含此 fix
> - 本项目 `cloud_adapter.cpp` 当前**未使用** `set_follow_location(true)` → **未触发**,但**未来引入风险高**
>
> 详见审计: `docs/audits/2026-09-10-adr-0087-sprint-24-httplib-status.md`
> §修订建议: Sprint 25 重定位至 **httplib 升级 0.18.4 → v0.39.0+ for security**,
> 保留 §Step 2 框架但替换升级理由。

### Step 3 — 验证清单

- Phase B B.2 (DomainWorkerPool(4) + 共享真实 deepseek provider) 无 SerializingDecorator 通过
- Phase E Skill IPC (skill_child_main 调 llm_generate) 多 worker 真并发通过
- Phase G ContextCompactor (DomainWorkerPool(>1) 摘要) 多 worker 真并发通过
- pdk_chat_demo 多 agent 真实 LLM 场景无 SIGSEGV

### Step 4 — 移除默认包装

`src/common/llm/llm_provider_factory.cpp` cloud 路径:
```cpp
// 修改前
if (backend == "openai" || ...) {
  auto adapter = cloud_factory->create(config);
  return std::make_unique<SerializingDecorator>(std::move(adapter), "cloud-" + backend);
}
// 修改后
if (backend == "openai" || ...) {
  // SerializingDecorator 升级后 OPT-IN (LLMProviderFactory::create(config, opts) opts.serializer)
  return cloud_factory->create(config);
}
```

### Step 5 — 性能 benchmark

| 配置 | 修改前 (含 SerializingDecorator) | 修改后 (移除默认包装) |
|---|---|---|
| 1 worker 真实 deepseek | t | t (无变化) |
| 4 worker 并发真实 deepseek | 4× 串行 (~12s) | 4× 并发 (~3s) |
| 8 worker 并发真实 deepseek (stress) | 8× 串行 (~24s) | 8× 并发 (~3-6s) |
| mock / llama 路径 | 无影响 (无包装) | 无变化 |

## 不变量风险

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| OpenSSL 3.0 ABI 不兼容外部下游 | 低 | 中 | OpenSSL 3.0 默认提供 1.1 兼容层; 文档记录 BREAKING |
| httplib upstream 未修复根因 | 中 | 中 | SerializingDecorator OPT-IN fail-safe; 性能税仍可承担 |
| 升级后回归 (单线程路径) | 低 | 高 | Step 2 + Step 3 验证清单 + Phase 0+1 测试套件 |
| 新引入多线程 OpenSSL 配置错误 | 中 | 中 | Step 1 SSL_CTX per-thread 验证 + 多 worker stress 测试 |

## 升级触发条件 (Escalation to P0)

本 ADR 升 P0 实施 (5-7 天独立 change) 当任一条件满足:
- Phase E/G 启用真实 LLM 后出现新的多线程 SIGSEGV 变种
- OpenSSL 发布 4.0 (升级收益更显著)
- httplib 发布明确的多线程 fix (升级路径清晰)
- 项目其他模块 (Skill IPC / pdk_chat_demo) 出现多线程 LLM 问题

每次 Sprint 收官 review 评估触发条件.

## 实施日志

### Sprint 24 调研 (2026-09-10)

**调研人**: Sisyphus session (Wave 4 series + 2 follow-ups ship 后)

**目标** (per handoff §3): OpenSSL 3.0 ABI audit + httplib issue #1903 fix status 调研

**调研产出**:
- `docs/audits/2026-09-10-adr-0087-sprint-24-openssl-audit.md` (本 ADR §Step 1 假设验证)
- `docs/audits/2026-09-10-adr-0087-sprint-24-httplib-status.md` (本 ADR §Step 2 假设验证 + CVE 发现)

**关键发现**:

1. **§Step 1 (OpenSSL 3.0 集成) — 0 LOC 迁移,已隐式完成**
   - 系统 OpenSSL 已是 3.0.13 (find_package 无版本约束,自动找最高)
   - `cloud_adapter.cpp` **0 行 OpenSSL 代码** (所有 SSL 操作封装在 httplib 内)
   - 不需要任何代码改动

2. **§Step 2 (httplib 升级 for threading) — 假设失效,真根因未明**
   - 真实 threading SIGSEGV fix = **PR #701** (merged Nov 29, 2020)
   - shipped in **v0.8.0 (Jan 12, 2021)**
   - 当前 vendored 0.18.4 **已含 PR #701** ([Oracle 修订]: `socket_requests_in_flight_` L1507-1509 + `ssl_new` with `ctx_mutex_` L8940-8968 + `request_mutex_` L1504 [原调研行号 L7477 漂移已修正] + `authorization_count_` L663)
   - **但 SIGSEGV 在含 PR #701 的 0.18.4 上仍被观测** (c0cb522 Phase B ship 实证) ⇒ **PR #701 存在不证明根因已修**,真根因未明
   - handoff §3.2 提到的 issue #1903 **与 threading 无关** (Windows file size 32-bit bug)
   - gdb backtrace (`create_client_socket` + `std::function<void(int)> this=0x11`) 指向 `ClientImpl::socket_options_` 损坏/use-after-free,与 PR #701 类 race 不一致

3. **§根因诊断偏差** — ADR §根因诊断 (line 82-84) "Authorization header 栈处理 bug" 假设**未引用 issue 编号**,且未与 PR #701 关联;准确表述为"两条假设均未证实,仅 2020 年代 race 可排除"

4. **🚨 多 client 侧 security advisories (Oracle 补充)**:
   - **CVE-2026-33745** (GHSA-6hrp-7fq9-3qv2, **High CVSS 7.4** [原 Critical 误标已修正]): Auth credentials leaked on cross-origin redirect; fixed in v0.39.0; 当前 0.18.4 未含
   - **GHSA-39q5-hh6x-jpxx** (High, 2026-03-10): 恶意 Content-Length 响应头使 client 进程崩溃;fixed in 后续版本;**cloud_adapter 直接暴露**
   - **GHSA-h6wq-j5mv-f3q8** (Moderate, 2026-05-12): 负 chunk-size DoS;**LLM streaming 直接暴露** (`generate_stream` 走 chunked transfer)
   - **GHSA-c3h8-fqq4-xm4g** (High conditional, 2026-03-13): proxy 下 HTTPS redirect TLS bypass;**当前未触发**(项目不用 proxy)
   - **覆盖建议**: 升级至 v0.54.1 (latest stable, 2026-08-30) 一次性覆盖 4/4 advisories

### Oracle 评审 (2026-09-10) — SHIP-with-fixes

**评审 session**: `ses_f760c60d5ffe9hmgREbWz0hA8u` (Oracle 5m17s)

**结论**: 方向 ✅,升级目标 v0.39.0 **不足**,需 3 High + 3 Medium 修正后启动 Sprint 25。

**修正清单** (已应用):

**High (已完成)**:
- **H1**: 升级目标 v0.39.0 → **v0.54.1** (覆盖 4/4 client 侧 advisories)
- **H2**: 严重度 Critical → **High (CVSS 7.4)** (修订审计 + ADR)
- **H3**: 删除"已含所有 threading fix / 无需升级即获 threading 安全"自相矛盾表述,改为"PR #701 修复的 2020 年代 race 已排除,真根因未明,21 版本累积修复可能覆盖"

**Medium (已完成)**:
- **M1**: 升级爆炸半径扩展 — httplib 使用点 = **6 个文件**:
  - 生产: `src/common/env/docker_backend.cpp:12` (Docker over Unix socket Client) + `src/common/llm/cloud_adapter.cpp:15` (HTTPS + Authorization Client) + `src/common/llm/http_adapter.cpp:10` (HTTP Client)
  - 测试: `tests/test_helpers/http_mock_server.h:8` + `tests/test_http_adapter.cpp:7` + `tests/test_docker_backend.cpp:28`
  - **Headers 迭代器构造** 模式需验证 v0.52.0 后兼容 (cloud_adapter L250, http_adapter L172)
  - **`set_follow_location` 0 处使用** ✅ 当前安全
  - **Sprint 25 回归 scope**: test_docker_backend + test_http_adapter + test_cloud_adapter_multithread
- **M2**: Sprint 26 实证方案补强 — **HTTPS + Authorization + chunked streaming 路径 + TSan preset + ≥3 次重复**(单次 green 不构成证据)
- **M3**: 升级前置验证 — vendored httplib.h (SHA256 `ca2fc115558d72942b3235afb35759a7056146cc55cf38063ea5645517024066`, 10325 行) **零本地 patch 标记**(grep TODO/FIXME/HydraForge/@local 0 命中);非 git submodule,需手动 diff vs upstream v0.18.4 tag 实证零漂移

**Low (已完成)**:
- **L1**: 审计行号漂移 — `request_mutex_` 实际声明在 L1504 (L7477 是 lock_guard 使用点);"0.18.4 稳定 4+ 年"不实 (0.18.x 是 2025 年版本)
- **L2**: `set_follow_location` CI grep 守卫 — 加入 Sprint 25 scope (`grep -rn "set_follow_location" src/ pdk/ examples/ tests/` 必须为空)

### Sprint 25 范围重定位建议 (Oracle 修正版)

| 原计划 | 建议改为 (Oracle 修正) | 理由 |
|--------|---------|------|
| §Step 1 OpenSSL 3.0 集成 (1-1.5 天) | **删除** | 0 LOC,已隐式完成 |
| §Step 2 httplib 升级 v0.39.0+ for **CVE-2026-33745** (1-1.5 天) | **httplib 升级 0.18.4 → v0.54.1** (覆盖 4/4 client 侧 advisories, 1-2 天) | v0.39.0 仅覆盖 1/4 advisories;v0.54.1 一次性覆盖 + 21 个版本累积 bug fixes |

### Sprint 26 实证方案 (Oracle 补强版)

1. 升级 httplib 到 v0.54.1 (前置 Sprint 25)
2. 暂时移除 SerializingDecorator 默认包装 (`llm_provider_factory.cpp:102-114`)
3. 跑 8 worker × 20 task stress (`test_cloud_adapter_multithread.cpp`),**必须 HTTPS + Authorization 真实路径**
4. **TSan preset + ≥3 次重复**(SIGSEGV 是概率事件,单次 green 不构成证据)
5. 跑 Phase E Skill IPC + Phase G ContextCompactor (多 worker 真并发) 同样覆盖
6. **如果零 SIGSEGV** → §Decision 1 "默认包装" 可移除,SerializingDecorator 真正 OPT-IN
7. **如果仍 SIGSEGV** → httplib 0.54.1 后根因更细,需 gdb 重诊断 (类似 c0cb522 Phase B 流程)
8. **Decision 3 opts.serializer 保持可逆** — 任何时刻可手动开启降级

### Sprint 27-28 (不变)

- Sprint 27: 移除默认 SerializingDecorator + Decision 3 OPT-IN 落地
- Sprint 28: 4 worker benchmark (~12s → ~3s) + ADR-0087 ✅ Approved

### GO/NO-GO 决策

**🟢 GO (修订后)** — Sprint 25 重定位至 httplib **v0.54.1** 升级 (4/4 client 侧 advisories),无需 OpenSSL 集成。Sprint 26 启动真根因实证 (Oracle 补强 HTTPS+TSan+重复方案)。

### 未解问题 (per handoff §7 + Oracle 补充)

- ✅ Q1 OpenSSL 3.0 ABI break 影响面 = 0 LOC (回答: 0)
- ✅ Q2 httplib upstream fix 是否已 merged = 是 (PR #701, in v0.8.0+);**但 PR #701 存在不证明根因已修** (Oracle 修订)
- 🆕 Q2-extra httplib 4 个 client 侧 security advisories — v0.54.1 覆盖 (Oracle 补充)
- ⏳ Q3 4 worker benchmark 预期 ~4× 加速 — 待 Sprint 28 实证 (前置 v0.54.1 升级)
- ✅ Q4 是否需同步升级 `external/async_simple/demo_example/CMakeLists.txt` OpenSSL 引用 = 否 (demo 路径)
- 🆕 Q6 (新) Sprint 25 升级 httplib 时是否需要 fork upstream PR — 否,直接用 v0.54.1 official tag
- 🆕 Q7 (新) 是否需要为 httplib v0.52.0 `Headers` insertion-ordered ABI break 做适配 — 待 Sprint 25 实施前编译验证 (本项目用法面窄,预计无影响)
- 🆕 Q8 (新) 是否需要为 v0.53.0 `WebSocketClient::connect` 返回 `ws::Result` 做适配 — **不需要**(本项目未用 WebSocket)
- 🆕 Q9 (新) 是否还有其他未发现的 client/server-side CVE 在 v0.54.1 之后发布 — **低概率**(已订阅 httplib security advisories 列表, set_follow_location grep 守卫覆盖 CVE-2026-33745)

### ADR-0087 状态演进

| 日期 | 状态 | 触发 |
|------|------|------|
| 2026-09-08 | 🔍 Proposed | ADR 创建 (Wave 1 #2 ship 后) |
| 2026-09-10 (commit `26749c2`) | 🟡 Partial | Sprint 24 调研完成,根因诊断修订 |
| 2026-09-10 (本 commit) | 🟡 Partial | Oracle 评审 High/Medium/Low 修正应用 |
| 2026-09-XX (待) | 🟡 Partial → ✅ Approved | Sprint 25 升级 ship + Sprint 26 实证后 |

---

## 兼容性

**BREAKING (升级时)**:
- OpenSSL 3.0 ABI 与 OpenSSL 1.1 不完全兼容 (外部依赖 OpenSSL 1.1 的项目需适配)
- `LLMProviderFactory::create(config)` 默认行为变化 (不再包装 SerializingDecorator)
  - 透明 (返回类型仍 `unique_ptr<ILLMProvider>`)
  - 但性能特征变化 (N worker → N 并发 vs N 串行)
- 解决: 引入 `LLMProviderFactory::create(config, opts)` opts.serializer = true
  提供显式降级开关

**OPT-IN SerializingDecorator**: 升级后 SerializingDecorator 类保留在代码库
- 默认不启用 (升级后无需)
- 诊断/降级用途可显式启用

## 验证清单

### 当前 (Wave 1 #2 ship 后)

- ✅ SerializingDecorator 实现完整 ship (mutex + cv 串行化)
- ✅ factory.create cloud 路径默认包装
- ✅ 4 unit + 1 integration + 1 stress 测试 PASS
- ✅ Phase B B.2 WARN+SUCCEED 占位移除 (B.2 启用)
- ✅ Oracle ship-gate APPROVE (Wave 1 #2)
- 🔍 ADR-XXXX 占位符已更新为 ADR-0087 (本 change 完成时)

### 升级时 (后续 change)

- OpenSSL 3.0 集成 + SSL_CTX per-thread 验证
- httplib 升级 + 回归 test_http_adapter
- Phase B B.2 / Phase E / Phase G 多 worker 真并发 PASS
- 默认包装移除 + opts.serializer OPT-IN 设计
- 性能 benchmark 报告 (4 worker: ~12s → ~3s)

## References

- **Wave 1 #2 实施**: `openspec/changes/fix-cloud-adapter-multithreading/`
  (ship commit `3d653e0` + Phase 2 commits)
- **Oracle ship-gate review**: session `ses_f7de8daa0ffeMA9GroBvS4MZOR` (P2 fix item)
- **追踪change scaffold**: `openspec/changes/cloud-adapter-threading-root-cause/`
- **根因诊断**: `real-llm-core-coverage` Phase B ship commit `c0cb522`
- **现有 ADR 编号**: `docs/adr/` 最大 ADR-0086 → 本 ADR-0087
- **SerializingDecorator 实现**: `src/common/llm/serializing_decorator.{h,cpp}`