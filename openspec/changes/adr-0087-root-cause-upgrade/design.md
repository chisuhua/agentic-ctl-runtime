# adr-0087-root-cause-upgrade — Design

## Context

ADR-0087 (🔍 Proposed) 记录 Wave 1 #2 `fix-cloud-adapter-multithreading` 通过工厂层 `SerializingDecorator` (mutex + cv) 规避 CloudLLMAdapter 多线程 SIGSEGV. 性能税 N→1 (并发退化为串行).

**Root Cause 诊断** (per Wave 1 #2 验证):
- OpenSSL 1.1+ SSL_CTX 非线程安全 (3.0 修复 per-thread init)
- httplib Authorization header + https 多线程组合 bug (issue #1903)
- 三者同时触发: N≥2 worker 并发 + Authorization header + https → create_client_socket 栈 corruption

**Foundation 已就绪** (Wave 4 ship):
- stop_token 透传链路: SkillInterpreter::run → ipc_loop_and_wait → dispatch → dispatch_llm_generate → llm_->generate
- SimpleCognitiveOrchestrator::process → react_once → llm_->generate
- node_executor → execute_yield → YieldStreamBridge → generate_stream
- ErrorCode::Cancelled + LLMError::Cancelled 映射

此 foundation 让 root cause 修复期间 cancel 语义保持一致.

## Goals / Non-Goals

**Goals**:
- OpenSSL 1.1+ → 3.0 集成 (per-thread SSL_CTX 初始化正确)
- httplib 升级到包含 issue #1903 fix 的 release
- 移除 LLMProviderFactory cloud 路径默认 SerializingDecorator 包装
- SerializingDecorator 转为 OPT-IN 降级路径 (opts.serializer = true 启用)
- 4 worker 并发 deepseek benchmark: 预期 ~4× 加速 (12s → 3s)

**Non-Goals**:
- 不删除 SerializingDecorator (保留为降级用途)
- 不修改 LLMProvider interface
- 不引入新 LLM backend
- 不修改 Wave 4 cancel chain (验证升级期间 cancel 语义保持)

## Decisions

### Decision 1: OpenSSL 3.0 集成 — find_package + per-thread SSL_CTX

**选择**: CMake find_package OpenSSL 3.0 (>= 3.0.0) + 链接 `OpenSSL::SSL` + `OpenSSL::Crypto`. 修复 CloudLLMAdapter 中 SSL_CTX 初始化路径, 确保每线程独立 SSL_CTX 或 thread-local 缓存.

**理由**:
- OpenSSL 1.1+ SSL_CTX 非线程安全是 Wave 1 #2 验证的根因之一
- 3.0 提供 per-thread API (`SSL_CTX` + `SSL` 实例正确分配)

**Alternatives considered**:
- 自实现 SSL_CTX mutex 包装: 增加维护成本, 复用 OpenSSL 3.0 标准 API 更稳 — 拒绝
- 升级到 BoringSSL: 不同 API, 改动大 — 拒绝

### Decision 2: httplib 升级 — upstream issue #1903 fix

**选择**: 升级 `external/cpp-httplib/httplib.h` 到 upstream 包含 issue #1903 fix 的 release.

**理由**:
- httplib Authorization header + https 多线程组合 bug 是 Wave 1 #2 验证的另一根因
- upstream fix 提供线程安全的 header 处理

**Alternatives considered**:
- 替换为 libcurl: 不同 API, 需重写 CloudLLMAdapter — 拒绝
- 自实现多线程安全的 header 处理: 与 upstream 维护成本对比 — 拒绝

### Decision 3: SerializingDecorator 转为 OPT-IN

**选择**: 升级完成后 `LLMProviderFactory::create(config, opts)` 默认不注入 SerializingDecorator. opts.serializer = true 时启用 (OPT-IN).

**理由**:
- 升级成功后无需串行化, 默认 false 释放 N→1 性能
- OPT-IN 保留降级路径: 诊断 httplib/OpenSSL 退化场景 + 紧急降级开关

**Alternatives considered**:
- 完全删除 SerializingDecorator: 失去降级开关 — 拒绝 (per ADR-0087 Decision 3)

### Decision 4: 5-step 顺序 (per ADR-0087 Decision 2)

**选择**:
1. OpenSSL 3.0 集成 (Sprint 25)
2. httplib 升级 (Sprint 26)
3. 验证 (Phase B B.2 4 worker + Phase E Skill IPC + Phase G ContextCompactor)
4. 移除默认 SerializingDecorator (Sprint 27)
5. benchmark + OPT-IN 文档 (Sprint 28)

**理由**:
- 每步独立可验证 + 可回滚
- OpenSSL 先于 httplib (根因诊断顺序)

**Alternatives considered**:
- 一次性全升级: 单次风险大, 调试难 — 拒绝

## Risks / Trade-offs

[Risk] OpenSSL 3.0 ABI 不兼容 (1.1+ vs 3.0 数据结构变化)
→ Mitigation: 升级前 audit 所有 OpenSSL API 调用点, 预期 ~10 处需更新

[Risk] httplib 升级 break 现有 SSL client 用法
→ Mitigation: 升级后 Phase B/E/G 现有测试覆盖 (回归守卫)

[Risk] 4 worker benchmark 实际加速 < 4× (因 LLM API 网络 I/O 占主导)
→ Mitigation: benchmark 验证, 接受真实结果

[Risk] 升级期间性能税回归 (默认串行化保留)
→ Mitigation: OPT-IN serializer 启用时 stderr warning + ops 文档

## Migration Plan

**5-Sprint 排期** (每 Sprint 1-1.5 天):

### Sprint 24 (本 change 后续)
- 调研 OpenSSL 3.0 API + httplib issue #1903 fix status
- 评估上游 release 兼容性

### Sprint 25
- OpenSSL 3.0 集成 (find_package + 链接 + SSL_CTX per-thread)
- 验证 Phase B B.2 测试

### Sprint 26
- httplib 升级 (upstream release)
- 验证 Phase E Skill IPC + Phase G ContextCompactor 测试

### Sprint 27
- LLMProviderFactory cloud 路径移除默认 SerializingDecorator
- 验证 OPT-IN serializer 路径

### Sprint 28
- 4 worker benchmark (预期 ~4× 加速)
- OPT-IN 文档 + ADR-0087 升级到 ✅ Approved

## Open Questions

- OpenSSL 3.0 升级是否会引入 ABI break (需 Sprint 24 调研)
- httplib issue #1903 fix 实际 release 状态 (需 Sprint 24 调研)
- 4 worker benchmark 是否能稳定达到 ~4× 加速 (需 Sprint 28 验证)