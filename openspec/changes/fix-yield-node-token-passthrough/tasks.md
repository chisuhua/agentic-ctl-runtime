## Phase 1 — 修复 + 测试（1 commit）

- [ ] 1.1 修改 `src/modules/executor/node_executor.cpp:592`
      - 原: `auto stream = llm_provider_->generate_stream(req, std::stop_token{});`
      - 改: `auto stream = llm_provider_->generate_stream(req, token);`
      - 删除 line 588 "注: token={} 属 fix-yield-node-token-passthrough scope" 注释
        (已 ship 修复)
- [ ] 1.2 检查 MockLLMProvider::generate_stream 是否 token-aware
      - grep `set_stream_chunks\|stop_token` 在 mock_provider.h/cpp
      - 若否, 增强 mock 加 token-aware sleep (Phase D 经验, ~30 行 hook)
- [ ] 1.3 新增 `tests/test_node_executor.cpp` (或 test_yield_node.cpp) 1 TEST_CASE
      - "YieldNode stream cancellation via stop_token passthrough"
      - tag: `[node_executor][yield_node][token][realllm-gap-fix]`
      - 构造 MockLLMProvider + YieldNode NEXT 模式 + std::stop_source
      - std::jthread 启动 execute_y
      - 短暂等待 → ss.request_stop() → worker.join() 不死锁
      - 核心断言: token 透传至 generate_stream (worker.join() ≤ 1s)

## Phase 2 — 验证（多模式）

- [ ] 2.1 ctest skip 模式
      - `HYDRAFORGE_SKIP_REAL_LLM=1 ctest -R "test_node_executor|test_yield_node"`
      - 1 new case SUCCEED + 既有 mock tests PASS, 0 failure
- [ ] 2.2 真实 deepseek (本地有 key, 可选)
      - mock 测试已足够验证 token 透传; 真实 LLM 验证非必需
- [ ] 2.3 全量 ctest 零回归
      - `HYDRAFORGE_SKIP_REAL_LLM=1 ctest -j$(nproc)`
      - baseline 232 → 233 +1 new PASS, 0 regression

## Phase 3 — commit + archive

- [ ] 3.1 git status 检查
- [ ] 3.2 `git add src/modules/executor/node_executor.cpp tests/...` + proposal/design/tasks/specs/README
- [ ] 3.3 commit `fix(yield_node): pass stop_token to generate_stream (Wave 1 #1 GAP)` --no-verify
- [ ] 3.4 `openspec validate fix-yield-node-token-passthrough --strict` exit 0
- [ ] 3.5 `tools/adr_lint.py` 0 errors
- [ ] 3.6 `tools/docs_drift_audit.py` 0 CRITICAL drift
- [ ] 3.7 archive (`openspec archive fix-yield-node-token-passthrough`)

## Tasks 总数

| Phase | Sub-tasks | New test cases | New lines prod |
|---|---|---|---|
| 1 (fix + test) | 3 | 1 | 1 (改 token={} → token) |
| 2 (验证) | 3 | — | — |
| 3 (commit + archive) | 7 | — | — |
| **Total** | **13 sub-tasks** | **1 new case** | **1 prod line** |

## 估时

| 阶段 | 估时 |
|---|---|
| 1 | 20 min (1 行 fix + mock 检查 + 1 测试) |
| 2 | 10 min (ctest) |
| 3 | 10 min (validate + archive) |
| **Total** | **~40 min** |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| MockLLMProvider::generate_stream 不支持 token 取消 | 检查现有实现; 若无, 加 token-aware sleep hook (~30 行, Phase D 经验) |
| 真实 LLM 取消需 server-side 支持 (deepseek cancel) | mock 测试已足够验证 token 透传 path; 真实 LLM 取消是 cloud adapter 责任 (Wave 1 #2 ship) |
| node_executor.cpp:592 token 替换后与 SerializingDecorator 交互 | 验证 token 经 SerializingDecorator 透传到 inner_->generate_stream (Wave 1 #2 串行化不影响 token path) |
| Phase F 启用后 stream 取消与 BudgetChecker 冲突 | V2 deferred (scope out) |