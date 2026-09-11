## 1. 调研 (Sprint 24) ✅ SHIPPED

- [x] 1.1 OpenSSL 3.0 ABI 调研: 所有 OpenSSL API 调用点 audit + 数据结构变化清单
  - **Commit:** `0a76de0 docs(audit): Sprint 24 OpenSSL 3.0 ABI + httplib upstream status audits`
- [x] 1.2 httplib issue #1903 fix status 调研: upstream release 包含 + 兼容性
  - **Commit:** `0a76de0` (同 1.1, 综合审计文档)
- [x] 1.3 ADR-0087 当前状态从 🔍 Proposed → 🟡 Partial (调研完成, 实施待启动)
  - **Commit:** `26749c2 docs(adr-0087): Sprint 24 调研完成 → 🟡 Partial (Step 1+2 假设偏差修订)`
- [x] 1.4 (verify) CloudLLMAdapter 中 SSL_CTX 使用路径 audit (~10 处预期需更新)
  - **状态:** 审计文档 `docs/audits/2026-08-04-sprint24-adr-0087-step1-2-audit.md` 已 ship

## 2. OpenSSL 3.0 集成 (Sprint 25) ✅ SHIPPED

- [x] 2.1 CMake: find_package OpenSSL 3.0 (>= 3.0.0)
  - **Commit:** `b97fdf8 feat(httplib): upgrade vendored cpp-httplib 0.18.4 → v0.54.1 (Sprint 25 security)`
  - **验证:** `find_package(OpenSSL REQUIRED)` 在 `CMakeLists.txt:25`
- [x] 2.2 链接 OpenSSL::SSL + OpenSSL::Crypto (验证已 ship)
  - **Commit:** `b97fdf8` 同上
  - **验证:** `CMakeLists.txt:118-119` 链接 OpenSSL::SSL + OpenSSL::Crypto
- [x] 2.3 CloudLLMAdapter: SSL_CTX per-thread 初始化 (或 thread-local 缓存)
  - **状态:** System OpenSSL 3.0.13 已 ship (per `cmake configure` 输出), CloudLLMAdapter 使用标准 API, 不需特殊处理
- [x] 2.4 验证 Phase B B.2 test_cloud_adapter_multithread 4 worker 真并发 PASS
  - **Commit:** `d2b3551 test(llm): B.2 enable + factory integration + multithread stress`
- [x] 2.5 (verify) 全量 ctest 228/228 PASS 零回归
  - **状态:** Sprint 25 收官验证 (Sprint 26 升级后变 229/229 → Sprint 27 升级后 230/230)

## 3. httplib 升级 (Sprint 26) ✅ SHIPPED

- [x] 3.1 升级 external/cpp-httplib/httplib.h 到 upstream 最新 release
  - **Commit:** `b97fdf8 feat(httplib): upgrade vendored cpp-httplib 0.18.4 → v0.54.1 (Sprint 25 security)`
  - **版本:** v0.54.1 (22669 行, 跨 36 minor 版本)
- [x] 3.2 Authorization header 多线程 fix 验证 (issue #1903)
  - **覆盖 security advisories:** CVE-2026-33745 + GHSA-39q5-hh6x-jpxx + GHSA-h6wq-j5mv-f3q8 + GHSA-c3h8-fqq4-xm4g
  - **Oracle 复核:** session `ses_f760c60d5ffe9hmgREbWz0hA8u`
- [x] 3.3 验证 Phase E Skill IPC 多 worker 测试 PASS
  - **状态:** `tests/test_skill_interpreter.cpp` 18 cases PASS (Sprint 22 ship)
- [x] 3.4 验证 Phase G ContextCompactor 多 worker 测试 PASS
  - **状态:** `tests/test_context_compactor.cpp` 多 worker 测试 PASS (Sprint 23 ship)
- [x] 3.5 (verify) 全量 ctest 229/229 PASS 零回归
  - **Commit:** `b97fdf8` ship 时验证 (baseline 228 + 新 test_httplib_version 1 = 229)

## 4. 移除默认 SerializingDecorator (Sprint 27) ✅ SHIPPED

- [x] 4.1 LLMProviderFactory::create(config, opts) cloud 路径默认不注入 SerializingDecorator
  - **Commit:** `de79309 feat(llm): ADR-0087 Step 4 — cloud 路径默认无 SerializingDecorator (OPT-IN 降级)`
  - **变更:** `src/common/llm/llm_provider_factory.{h,cpp}` 新增 `CreateOptions` struct + 2 参 create 重载; 1 参 create 委托 2 参 (向后兼容); cloud 路径 `if (opts.serializer)` 才包装 SerializingDecorator
- [x] 4.2 opts.serializer = true 启用 OPT-IN 路径 (诊断 + 紧急降级)
  - **测试:** `tests/test_llm_provider_factory_decorator.cpp` 7 cases (4 default 无包装 + 3 OPT-IN 路径)
  - **stderr warning:** `src/common/llm/llm_provider_factory.cpp` opts.serializer=true 时打印 warning (per design.md Risk mitigation)
- [x] 4.3 验证 Phase B/E/G 现有测试零回归
  - **test_cloud_adapter_multithread (Phase B B.2)**: PASS 12.56s (8 worker × 20 task real deepseek, root cause fix 验证)
  - **test_skill_interpreter (Phase E)**: PASS
  - **test_context_compactor (Phase G)**: PASS
- [x] 4.4 (verify) 全量 ctest 230/230 + 1 pre-existing cognitive_worker fail 零新增回归
  - **Oracle SHIP-with-fixes 修正 commit:** `9d6d6a6 fix(adr-0087-step4): Oracle SHIP-with-fixes 修正 (5 项, de79309 后续)`
  - **Oracle session:** `ses_f738bec89ffeFMay5d3VIiXJGd` SHIP-with-fixes verdict
  - **验证:** openspec validate --strict PASS / adr_lint 68 ADR PASS / docs_drift_audit 0 DRIFT / check-model-default-cleared 8/8 OK

## 5. Benchmark + OPT-IN 文档 (Sprint 28) ⏸ PENDING

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