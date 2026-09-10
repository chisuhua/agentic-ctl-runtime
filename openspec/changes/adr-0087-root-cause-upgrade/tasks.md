## 1. 调研 (Sprint 24)

- [ ] 1.1 OpenSSL 3.0 ABI 调研: 所有 OpenSSL API 调用点 audit + 数据结构变化清单
- [ ] 1.2 httplib issue #1903 fix status 调研: upstream release 包含 + 兼容性
- [ ] 1.3 ADR-0087 当前状态从 🔍 Proposed → 🟡 Partial (调研完成, 实施待启动)
- [ ] 1.4 (verify) CloudLLMAdapter 中 SSL_CTX 使用路径 audit (~10 处预期需更新)

## 2. OpenSSL 3.0 集成 (Sprint 25)

- [ ] 2.1 CMake: find_package OpenSSL 3.0 (>= 3.0.0)
- [ ] 2.2 链接 OpenSSL::SSL + OpenSSL::Crypto (验证已 ship)
- [ ] 2.3 CloudLLMAdapter: SSL_CTX per-thread 初始化 (或 thread-local 缓存)
- [ ] 2.4 验证 Phase B B.2 test_cloud_adapter_multithread 4 worker 真并发 PASS
- [ ] 2.5 (verify) 全量 ctest 228/228 PASS 零回归

## 3. httplib 升级 (Sprint 26)

- [ ] 3.1 升级 external/cpp-httplib/httplib.h 到 upstream 最新 release
- [ ] 3.2 Authorization header 多线程 fix 验证 (issue #1903)
- [ ] 3.3 验证 Phase E Skill IPC 多 worker 测试 PASS
- [ ] 3.4 验证 Phase G ContextCompactor 多 worker 测试 PASS
- [ ] 3.5 (verify) 全量 ctest 228/228 PASS 零回归

## 4. 移除默认 SerializingDecorator (Sprint 27)

- [ ] 4.1 LLMProviderFactory::create(config, opts) cloud 路径默认不注入 SerializingDecorator
- [ ] 4.2 opts.serializer = true 启用 OPT-IN 路径 (诊断 + 紧急降级)
- [ ] 4.3 验证 Phase B/E/G 现有测试零回归
- [ ] 4.4 (verify) 全量 ctest 228/228 PASS 零回归

## 5. Benchmark + OPT-IN 文档 (Sprint 28)

- [ ] 5.1 4 worker 并发 deepseek benchmark (预期 ~4× 加速, baseline ~12s → fix 后 ~3s)
- [ ] 5.2 OPT-IN serializer 路径文档 (docs/active-status.md + AGENTS.md)
- [ ] 5.3 ADR-0087 状态从 🟡 Partial → ✅ Approved
- [ ] 5.4 openspec archive adr-0087-root-cause-upgrade

## 6. 关联 (待 ship 后)

- [ ] 6.1 active-status.md 更新: ADR-0087 root cause upgrade 完成记录
- [ ] 6.2 ctest 计数 + 性能 benchmark 数据
- [ ] 6.3 AGENTS.md §ENGINEERING PATTERNS 沉淀 (Multithread SIGSEGV root cause 解决)

## 7. Validation Per Sprint

- [ ] cmake --build 零 error + 零 warning
- [ ] openspec validate --strict PASS
- [ ] adr_lint PASS (68 ADR, +ADR-0087)
- [ ] docs_drift_audit: 0 DRIFT items
- [ ] check-model-default-cleared 8/8 OK