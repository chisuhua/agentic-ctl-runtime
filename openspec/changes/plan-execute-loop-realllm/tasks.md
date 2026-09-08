## Phase 1 — 测试代码（1 commit）

- [ ] 1.1 新建 `tests/test_plan_execute_realllm.cpp`
      - includes: `agenticdsl/pdk/agent_loops/plan_execute_loop.h` + `loop_result.h` +
        `tests/test_helpers/real_llm_env.h` + 既有 mock 测试的 DSL 模板
      - helper lambda: `make_engine_with_echo()` (与 Phase A.2 模式一致)
      - TEST_CASE 1: C.1 plan_phase 真实 LLM 合法 DSL
      - TEST_CASE 2: C.2 verify_phase 真实 LLM 含 "yes"
      - TEST_CASE 3: C.3 end-to-end run("compute 2+3") 真实 LLM
      - 每个 case 头部: `require_real_llm_env()` + `real_llm_env_skipped()` short-circuit
      - 每个 case 失败路径: `std::cerr` 诊断输出 (仿 Phase A.4 模式)
- [ ] 1.2 验证 CMake GLOB 自动注册新 target
      - `tests/CMakeLists.txt` 无变更 (file(GLOB "test_*.cpp") 已覆盖)
      - `cmake --build build --target test_plan_execute_realllm -j$(nproc)` PASS

## Phase 2 — 验证（多模式）

- [ ] 2.1 skip 模式 ctest (CI 默认场景)
      - `HYDRAFORGE_SKIP_REAL_LLM=1 ctest -R test_plan_execute_realllm`
      - 3 cases SUCCEED, 0 failure
- [ ] 2.2 真实 deepseek (本地有 key)
      - `./tests/test_plan_execute_realllm`
      - 至少 C.1 或 C.2 PASS (验证链路); C.3 可能 flake (依赖两次 LLM 串行)
      - 失败 case 记录 (message + retries_used) 到本地日志
- [ ] 2.3 全量 ctest 零回归
      - `HYDRAFORGE_SKIP_REAL_LLM=1 ctest -j$(nproc)`
      - baseline 224 → 227 +3 new PASS, 0 regression
- [ ] 2.4 既有 test_plan_execute_restart.cpp 零回归
      - `HYDRAFORGE_SKIP_REAL_LLM=1 ctest -R test_plan_execute_restart`
      - 3 mock cases PASS

## Phase 3 — commit + archive

- [ ] 3.1 git status 检查 (新文件 + tasks.md 标记)
- [ ] 3.2 `git add tests/test_plan_execute_realllm.cpp` + tasks.md + spec.md + proposal.md + design.md + README.md
- [ ] 3.3 commit `test(plan_execute_loop): real-llm Phase C — plan/verify/end-to-end 真实 deepseek` --no-verify
- [ ] 3.4 `openspec validate plan-execute-loop-realllm --strict` exit 0
- [ ] 3.5 `tools/adr_lint.py` 0 errors
- [ ] 3.6 `tools/docs_drift_audit.py` 0 CRITICAL drift
- [ ] 3.7 archive (`openspec archive plan-execute-loop-realllm`)

## Tasks 总数

| Phase | Sub-tasks | New test cases |
|---|---|---|
| 1 (代码) | 2 | 3 (real LLM, 1 文件) |
| 2 (验证) | 4 | — |
| 3 (commit + archive) | 7 | — |
| **Total** | **13 sub-tasks** | **3 new cases** |

## 估时

| Phase | 估时 |
|---|---|
| 1 | 1 h |
| 2 | 1 h (含真实 LLM 调试) |
| 3 | 30 min |
| **Total** | **~2.5 h** |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| plan_phase / verify_phase model 遮蔽未修 → 真实 LLM 必撞墙 | 依赖 `fix-generation-request-model-default` 先 ship; 若未 ship, 本 change 真实 LLM 部分必 FAIL, 记录为该 fix-up change 的 blocker |
| plan_phase LLM 输出无法解析 DSL | prompt 调整 (在测试内 prompt 措辞) 或记录为 prompt 改进 follow-up |
| verify_phase LLM 不返回 "yes" | 大小写不敏感 substring 已 ship; 但 LLM 可能 "yes." "Yes, success" 等, 验证 "yes" 子串覆盖 |
| PlanExecuteLoop.plan_phase / verify_phase 是 private, 测试无法直接调 | 走 run() 间接验证 (C.1/C.3); 或 friend 声明 (本 change 不引入) |
| PlanExecuteLoop 多次 generate 累积延迟 (C.3 end-to-end ~30-60s) | wait_until 120s 超时充足; 性能非本 change 重点 |
| 既有 test_plan_execute_restart.cpp 因 set_llm_provider 改变 mock 行为 | 既有测试用 enqueue_response 不依赖 set_llm_provider; 本 change 不改 engine llm provider (PlanExecuteLoop 构造传入 real provider, engine 保持 mock) |