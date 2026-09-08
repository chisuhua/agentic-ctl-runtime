# ADR-0087: Cloud Adapter Threading Model (多线程SIGSEGV 根因修复路径)

**日期**: 2026-09-08
**父主题**: fix-cloud-adapter-multithreading Wave 1 #2 (SerializingDecorator) 后续根因修复

## 状态

🔍 Proposed (追踪物, 实施升级时升级到 Approved)

> **背景 (2026-09-08)**: Wave 1 #2 `fix-cloud-adapter-multithreading` 通过工厂层
> `SerializingDecorator` (mutex + cv 串行化 generate/generate_stream) 规避了
> CloudLLMAdapter 多线程 SIGSEGV — **这是规避, 不是根因修复**. 本 ADR 记录根因
> 诊断 + 升级路径, 防止 N→1 性能税意外固化为永久架构.
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

### Step 2 — httplib 升级

调研 upstream commit history:
- 是否已修复 Authorization header 多线程栈 corruption
- API 变更 (compat with current `external/httplib/httplib.h` usage)

集成:
- 替换 `external/httplib/httplib.h` 为 upstream 最新版
- 回归测试现有 `tests/test_http_adapter.cpp` 单线程路径

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