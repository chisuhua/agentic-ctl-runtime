## Why

ADR-0087 (🔍 Proposed) 记录 Wave 1 #2 `fix-cloud-adapter-multithreading` 通过工厂层 `SerializingDecorator` (mutex + cv) 规避 CloudLLMAdapter 多线程 SIGSEGV — **这是规避, 不是根因修复**. 实施 root cause 修复可移除 N→1 性能税, 让 4 worker 并发 deepseek: baseline 串行 ~12s → fix 后并发 ~3s.

本 change 是 Wave 4 token passthrough (8 OpenSpec changes) 闭环后启动的下一个长期 upgrade track. Wave 4 已 ship: stop_token 透传 (3 changes) + Cancel 错误码语义统一 (#1) + dispatch_llm_generate token 透传 (#2), 为 ADR-0087 root cause 修复建立完整 cancel chain.

## What Changes

**Sprint 24+ 5-step root cause 修复路径** (per ADR-0087 Decision 2):

1. **OpenSSL 1.1+ → 3.0 集成** — find_package OpenSSL 3.0 + 链接 + SSL_CTX per-thread 正确初始化
2. **httplib 升级** — upstream 多线程 Authorization header fix 调研 + 集成 (`external/cpp-httplib/httplib.h` issue #1903 提到的 fix)
3. **现有测试验证** — Phase B B.2 (4 worker 真并发) + Phase E Skill IPC 多 worker + Phase G ContextCompactor 多 worker 真并发
4. **移除 `LLMProviderFactory` cloud 路径默认 `SerializingDecorator` 包装** — 升级成功后默认 false
5. **性能 benchmark** — 4 worker 并发 deepseek: baseline 串行 ~12s vs fix 后并发 ~3s (预期 ~4× 加速)

**架构转换**:
- 升级前: CloudLLMAdapter → SerializingDecorator (mutex) → 真实 adapter (强制串行)
- 升级后: CloudLLMAdapter → 真实 adapter (并发安全, 无 SerializingDecorator)
- OPT-IN 路径: `LLMProviderFactory::create(config, opts)` opts.serializer = true → SerializingDecorator 包装 (诊断 + 紧急降级)

## 当前 Foundation 已就绪

Wave 4 token passthrough 提供 cancel 链路:
- `SkillInterpreter::run(token)` → `ipc_loop_and_wait(token)` (Wave 4 #3) → `dispatch(token)` (Wave 4 #2) → `dispatch_llm_generate(token)` → `llm_->generate(token)` → `CloudLLMAdapter::generate(token)` (Wave 1 #2 token-aware)
- `SimpleCognitiveOrchestrator::process(token)` → `react_once(token)` → `llm_->generate(token)` (Wave 4 #1)
- `node_executor` → `execute_yield(token)` → `YieldStreamBridge{token}` → `generate_stream(token)` (Wave 4 #0)
- ErrorCode::Cancelled (Wave 4 #1 follow-up) + LLMError::Cancelled 映射

此 foundation 让 root cause 修复期间的 cancel 语义保持一致 (任何 worker 并发场景下, 外部 cancel 都能透传至 LLM 调用).

## Capabilities

### New Capabilities
- (无 — 修改既有 cloud-adapter-threading capability)

### Modified Capabilities
- `cloud-adapter-threading`: 移除默认 SerializingDecorator 包装, 升级 OpenSSL/httplib, 加 OPT-IN 降级路径

## Impact

**代码影响**:
- `src/common/llm/serializing_decorator.h/cpp` — 保留为 OPT-IN, 不删除
- `src/common/llm/llm_provider_factory.cpp` — cloud 路径移除默认 SerializingDecorator 包装
- `external/cpp-httplib/httplib.h` — 升级到 upstream 最新 release
- `CMakeLists.txt` — find_package OpenSSL 3.0 (若尚未)
- `src/common/llm/cloud_llm_adapter.cpp` — SSL_CTX per-thread 正确初始化 (若有 bug)

**测试影响**:
- Phase B B.2: test_cloud_adapter_multithread 4 worker 真并发 (已 ship via Wave 1 #2, 现有测试验证)
- Phase E Skill IPC: test_skill_interpreter 7.8b/7.8c mid-run cancel (Wave 4 #3 验证)
- Phase G ContextCompactor: test_context_compactor 多 worker (既有)

**风险**: 中 (root cause 修复涉及底层 SSL/TLS + HTTP client 多线程安全, 多 worker 场景下可能暴露未覆盖的 bug)

## Non-goals

- **不删除** SerializingDecorator (升级后转为 OPT-IN 降级用途)
- **不修改** LLMProvider interface (保持向后兼容)
- **不引入** 新 LLM backend (Scope 限定 OpenSSL 3.0 + httplib 升级)

## 升级触发

Sprint 24+ 排期 (per ADR-0087 Decision 2 顺序):
1. Sprint 24 setup (调研 OpenSSL 3.0 + httplib issue #1903 fix status)
2. Sprint 25 OpenSSL 3.0 集成
3. Sprint 26 httplib 升级
4. Sprint 27 验证 + 移除默认 SerializingDecorator
5. Sprint 28 benchmark + OPT-IN 文档

## 估时

5-7 天 (per ADR-0087 Decision 2). 建议分 5 个 Sprint, 每 Sprint 1-1.5 天, 单 session 不可完成.

## 关联文档
- `docs/adr/adr-0087-cloud-adapter-threading-model.md` (parent ADR)
- `openspec/changes/cloud-adapter-threading-root-cause/` (existing scaffold, 5 files)
- `openspec/changes/fix-cloud-adapter-multithreading/` (Wave 1 #2 已 ship)
- AGENTS.md §ENGINEERING PATTERNS (Multithread SIGSEGV 根因 沉淀)