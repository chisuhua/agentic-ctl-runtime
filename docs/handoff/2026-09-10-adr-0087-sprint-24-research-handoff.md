# ADR-0087 Sprint 24 调研 Handoff

**日期**: 2026-09-10
**Handoff from**: 当前 session (Wave 4 + 2 follow-ups + ADR-0087 scaffold 已 ship)
**Handoff to**: 下个 session (Sprint 24 调研启动)

---

## 1. 项目状态速览

### 1.1 当前 git state

```
main branch ahead of origin: 大量 commits
ctest: 228/228 PASS (含 1 pre-existing flaky test_execute_parallel, rerun PASS)
adr_lint: 68 ADR PASS
docs_drift_audit: 0 DRIFT items
check-model-default-cleared: 8/8 sites OK
```

### 1.2 最近 session 累计交付 (Wave 4 series)

| Change | Status | Archive |
|---|---|---|
| `fix-yield-node-token-passthrough` | ✅ SHIP+ARCHIVED | `2026-09-10-fix-yield-node-token-passthrough` |
| `fix-orchestrator-token-passthrough` | ✅ SHIP+ARCHIVED | `2026-09-10-fix-orchestrator-token-passthrough` |
| `fix-skill-interpreter-token-and-timeout` | ✅ SHIP+ARCHIVED | `2026-09-10-fix-skill-interpreter-token-and-timeout` |
| `fix-cancel-errorcode-semantics` | ✅ SHIP+ARCHIVED | `2026-09-10-fix-cancel-errorcode-semantics` |
| `fix-skill-interpreter-dispatch-llm-token` | ✅ SHIP+ARCHIVED | `2026-09-10-fix-skill-interpreter-dispatch-llm-token` |
| ADR-0087 scaffold (planning/handoff) | ✅ Scaffold | `openspec/changes/adr-0087-root-cause-upgrade/` |

5 changes全部 Oracle APPROVE/SHIP, ctest 零回归, AGENTS.md 模式沉淀 +5 条.

---

## 2. ADR-0087 背景与现状

### 2.1 父 ADR

`docs/adr/adr-0087-cloud-adapter-threading-model.md` — 当前 🔍 Proposed, 记录:
- Wave 1 #2 `fix-cloud-adapter-multithreading` 通过 `SerializingDecorator` (mutex + cv) **规避** CloudLLMAdapter 多线程 SIGSEGV
- 根因诊断: OpenSSL 1.1+ SSL_CTX 非线程安全 + httplib Authorization header 多线程 bug
- 三者同时触发: N≥2 worker + Authorization header + https → `create_client_socket` 栈 corruption

### 2.2 ADR-0087 4 个 Decision (per ADR 全文)

- **Decision 1**: 当前默认 SerializingDecorator 包装保持 (规避)
- **Decision 2**: 根因修复 5 步骤 (OpenSSL 3.0 → httplib → 验证 → 移除默认 → benchmark)
- **Decision 3**: SerializingDecorator 升级后保留为 OPT-IN 降级 (`opts.serializer = true`)
- **Decision 4**: ADR-XXXX 占位符 → ADR-0087 替换 (已完成)

### 2.3 5-Sprint 排期 (per ADR-0087 Decision 2 + scaffold)

| Sprint | Scope | 估时 |
|---|---|---|
| **24 (本 handoff 启动点)** | OpenSSL 3.0 ABI audit + httplib issue #1903 fix status 调研 | 2-3 小时 |
| 25 | OpenSSL 3.0 集成 (find_package + per-thread SSL_CTX) | 1-1.5 天 |
| 26 | httplib 升级 (issue #1903 fix) | 1-1.5 天 |
| 27 | 移除默认 SerializingDecorator (OPT-IN serializer) | 1 天 |
| 28 | 4 worker benchmark (~12s → ~3s) + ADR-0087 ✅ Approved | 1 天 |

---

## 3. Sprint 24 调研任务

### 3.1 OpenSSL 3.0 ABI 调研

**目标**: 列出 CloudLLMAdapter 中所有 OpenSSL API 调用点 + 评估 3.0 ABI 兼容性

**入口**:
- `src/common/llm/cloud_llm_adapter.cpp` (主文件, 预期 ~10 处 OpenSSL API 调用)
- `CMakeLists.txt:25` — `find_package(OpenSSL REQUIRED)` (当前版本未知)
- `external/async_simple/demo_example/CMakeLists.txt:63` — 次要引用

**调研步骤**:
1. grep 所有 OpenSSL API 调用:
   ```bash
   grep -rn "SSL_CTX\|SSL_new\|SSL_connect\|BIO_new\|EVP_\|X509_" \
        src/common/llm/cloud_llm_adapter.cpp
   ```
2. 列出当前 OpenSSL 版本 (CMake config output):
   ```bash
   cmake -LA build | grep -i openssl
   ```
3. 对比 OpenSSL 1.1 → 3.0 已知 break points:
   - `SSL_CTX` API: 大部分兼容
   - `EVP_CIPHER` 引用: 1.1 deprecated API 在 3.0 移除
   - `ENGINE_*`: 3.0 移除 (若使用需迁移到 provider API)
   - `ERR_load_*`: deprecated, 3.0 自动加载
4. 评估 impact: 列出需要修改的 API 调用点 + 估算改动行数.

**产出**: 在 `docs/audits/2026-09-XX-adr-0087-sprint-24-openssl-audit.md` 写入审计报告.

### 3.2 httplib issue #1903 fix status 调研

**目标**: 确认 httplib upstream 已修复 issue #1903 + 找到含 fix 的 release 版本号

**入口**:
- `external/cpp-httplib/httplib.h:3011` — 已有 issue #1903 引用 (在 s += 注释附近)
- `external/cpp-httplib/httplib.h` 当前 commit hash / version 标记

**调研步骤**:
1. 查看当前 httplib.h 版本标记:
   ```bash
   grep -n "CPPHTTPLIB_VERSION\|CPPHTTPLIB_KEEPALIVE" external/cpp-httplib/httplib.h | head -5
   ```
2. 上 GitHub 查 issue #1903 + PR:
   - https://github.com/yhirose/cpp-httplib/issues/1903
   - 找到 fix PR commit hash + merged release
3. 对比当前 httplib.h vs upstream 最新 release:
   - 计算 commits between
   - 评估 merge conflict 风险
4. 评估 Authorization header 多线程 fix 的具体代码变化.

**产出**: 在 `docs/audits/2026-09-XX-adr-0087-sprint-24-httplib-status.md` 写入调研报告.

### 3.3 ADR-0087 状态升级调研 → 🟡 Partial

**目标**: 调研完成后 ADR-0087 从 🔍 Proposed → 🟡 Partial (类似 ADR-0072 翻牌模式)

**操作**:
```bash
# 修改 docs/adr/adr-0087-cloud-adapter-threading-model.md
# "## 状态" 行: 🔍 Proposed → 🟡 Partial
# 添加 §实施日志 段落记录 Sprint 24 调研完成
# 提交:
git add docs/adr/adr-0087-cloud-adapter-threading-model.md
git commit -m "docs(adr-0087): flip to 🟡 Partial (Sprint 24 调研完成)"
```

### 3.4 调研产出汇总

完成调研后, 给出 GO/NO-GO 决策:
- **GO**: 进入 Sprint 25 OpenSSL 3.0 集成
- **NO-GO**: 推迟 ADR-0087 (例如 OpenSSL 3.0 ABI break 影响面 > 预期)

---

## 4. 关键技术细节 (给下个 session)

### 4.1 Wave 4 cancel chain foundation (已完成, 提供 baseline)

```
外部 cancel token
  ↓
SkillInterpreter::run(skill, cap, token)              [已 ship]
  ↓
Impl::run(skill, cap, token)                            [已 ship]
  ↓
ipc_loop_and_wait(pid, ..., cap, token)                 [已 ship, 顶部 SIGKILL on cancel]
  ↓
while(true) {
  dispatch(req, cap, pid, token)                       [已 ship]
  ↓
  dispatch_llm_generate(req, cap, token)               [已 ship, 早期 early-exit 已 ship]
  ↓
  llm_->generate(gen_req, token)                        [已 ship]
  ↓
  CloudLLMAdapter::generate(req, token) → 立即响应 stop_token
}
```

**意义**: Sprint 25-28 root cause 修复期间, cancel 语义保持一致. 任何 worker 并发场景下, 外部 cancel 都能透传至 LLM 调用 (即使 SerializingDecorator 移除后).

### 4.2 SerializingDecorator 当前状态

`src/common/llm/serializing_decorator.h/cpp` — 当前默认包装 cloud 路径 (Wave 1 #2 ship):
```cpp
// factory 创建 cloud 路径默认注入 SerializingDecorator
auto llm = SerializingDecorator(std::make_unique<CloudLLMAdapter>());
```

**升级后**: 默认移除 SerializingDecorator, `opts.serializer = true` 启用 (Decision 3).

### 4.3 现有测试覆盖 (验证基础)

- Phase B B.2: `tests/test_cloud_adapter_multithread.cpp` — 4 worker 真并发 (Wave 1 #2 ship, 13 tests)
- Phase E: `tests/test_skill_interpreter.cpp` — 7.8b/7.8c/7.8d/7.8e (Wave 4 ship)
- Phase G: `tests/test_context_compactor.cpp` — 多 worker (既有)

### 4.4 风险热点 (供 Sprint 25-28 参考)

- `external/cpp-httplib/httplib.h` 单文件 ~10000+ 行, merge conflict 高
- CloudLLMAdapter SSL_CTX 使用路径需 audit (预期 ~10 处)
- benchmark 需在隔离环境跑 (4 worker deepseek, 网络 I/O 占主导, 真实结果可能 < 4× 加速)

---

## 5. 下个 session 启动 checklist

### 5.1 环境确认

```bash
cd /workspace/project/HydraForge
git log --oneline -10          # 应看到 b033eb8 (active-status Wave 4 follow-up #2 Oracle)
git status --short             # 应 clean
HYDRAFORGE_SKIP_REAL_LLM=1 ctest --test-dir build -j$(nproc)  # 应 228/228 PASS
python3 tools/adr_lint.py      # 应 PASS
python3 tools/docs_drift_audit.py  # 应 0 DRIFT
```

### 5.2 Sprint 24 调研启动命令

```bash
# Step 1: OpenSSL 3.0 ABI audit
mkdir -p docs/audits
grep -rn "SSL_CTX\|SSL_new\|SSL_connect\|BIO_new\|EVP_\|X509_" \
     src/common/llm/cloud_llm_adapter.cpp | tee docs/audits/sprint-24-openssl-audit-raw.txt

# Step 2: 查看当前 OpenSSL 版本
cmake -LA build | grep -i openssl

# Step 3: httplib 当前版本
grep -n "CPPHTTPLIB_VERSION" external/cpp-httplib/httplib.h

# Step 4: (optional) 上 GitHub 看 issue #1903
# https://github.com/yhirose/cpp-httplib/issues/1903
```

### 5.3 调研完成 → 翻牌 + 决策

```bash
# 编辑 ADR-0087 状态
sed -i 's|🔍 Proposed|🟡 Partial|' docs/adr/adr-0087-cloud-adapter-threading-model.md
# 添加 §实施日志 段落
git add docs/adr/adr-0087-cloud-adapter-threading-model.md
git commit -m "docs(adr-0087): Sprint 24 调研完成 → 🟡 Partial"
```

---

## 6. 关联文档与引用

### 6.1 必读 ADR

- `docs/adr/adr-0087-cloud-adapter-threading-model.md` — 父 ADR
- `docs/adr/adr-0021-pdk-tool-registration.md` — DECLARE_TOOL + SerializingDecorator 注册
- `docs/adr/adr-0034-model-router-plugin.md` — model_router PDEK plugin 范式 (httplib 升级时参考)

### 6.2 必读 OpenSpec

- `openspec/changes/adrs-0087-root-cause-upgrade/` — 本 change scaffold (5 artifacts)
- `openspec/changes/fix-cloud-adapter-multithreading/` — Wave 1 #2 已 ship (SerializingDecorator 引入)
- `openspec/changes/archive/2026-09-10-*` — Wave 4 series 5 changes

### 6.3 必读 AGENTS.md 模式

- ENGINEERING PATTERNS #1: 测试驱动发现生产 bug 闭环 (调试方法论)
- ENGINEERING PATTERNS #4: SHIP-with-fixes 流程 (Oracle ship-gate)
- tests/AGENTS.md: REAL-LLM TEST PATTERNS (Phase B/E/G 测试设计参考)

### 6.4 关键代码入口

```
src/common/llm/cloud_llm_adapter.cpp           # OpenSSL API audit 入口
src/common/llm/serializing_decorator.h/cpp    # OPT-IN serializer 改造入口
src/common/llm/llm_provider_factory.cpp       # 默认 SerializingDecorator 注入位置
external/cpp-httplib/httplib.h                # httplib 升级 target
CMakeLists.txt:25                             # OpenSSL find_package
```

---

## 7. 已知未决策项 (供 Sprint 24 调研回答)

1. OpenSSL 3.0 ABI break 影响面 (预期 ~10 处, 实际数量?)
2. httplib upstream fix 是否已 merged + 在哪个 release
3. 4 worker benchmark 预期 ~4× 加速, 实际比例?
4. 是否需要同步升级 `external/async_simple/demo_example/CMakeLists.txt` 的 OpenSSL 引用?
5. Sprint 25 实施时是否需要先开 PR fork upstream cpp-httplib (若有未公开的 fork patch)?

---

## 8. 用户已决策项 (不再讨论)

- ✅ ADR-0087 root cause 升级路径确定 (5-Sprint)
- ✅ SerializingDecorator 升级后保留为 OPT-IN
- ✅ Sprint 24 调研启动点已就绪
- ✅ Wave 4 cancel chain foundation 已 ship (提供 cancel 语义保证)

---

**Handoff ready**. 下个 session 直接根据 §5 启动 checklist 开始调研. Sprint 24 调研结果 (GO/NO-GO) 决定 Sprint 25-28 排期.