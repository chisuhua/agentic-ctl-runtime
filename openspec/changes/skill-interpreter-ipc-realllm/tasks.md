## Phase 1 — 公开 wrapper + tests（1 commit）

- [ ] 1.1 公开 `SkillInterpreter::call_llm_generate_for_test(req, cap)` wrapper
      — 位置: `include/agenticdsl/skill/skill_interpreter.h`
      — 标记 `[[deprecated("test only")]]` (test-only, 非 production API)
      — 转发到 `impl_->dispatch_llm_generate(req, cap)`
      — PIMPL 模式: SkillInterpreterImpl private dispatch 封装不变
- [ ] 1.2 扩展 `tests/test_skill_interpreter.cpp` 加 1 TEST_CASE
      — E.1: dispatch_llm_generate 真实 LLM host function direct test
      — `require_real_llm_env()` + `real_llm_env_skipped()` short-circuit
      — 构造 SkillCapability{allow_llm=true, budget_limit_usd=1.0}
      — 构造 IPCRequest{call="llm_generate", params={prompt, model}}
      — 调 `call_llm_generate_for_test(req, cap)`
      — 断言: `response.ok` + `response.data["content"]` 非空
      — 失败路径: `std::cerr` 诊断
- [ ] 1.3 验证 CMake GLOB 自动注册新 case
      - `tests/CMakeLists.txt` 无变更
      - `cmake --build build --target test_skill_interpreter -j$(nproc)` PASS

## Phase 2 — 验证（多模式）

- [ ] 2.1 skip 模式 ctest (CI 默认场景)
      - `HYDRAFORGE_SKIP_REAL_LLM=1 ctest -R test_skill_interpreter`
      - 1 new case SUCCEED + N mock cases PASS, 0 failure
- [ ] 2.2 真实 deepseek (本地有 key)
      - `./tests/test_skill_interpreter`
      - E.1 PASS (response.ok=true, content 非空)
      - 失败 case 记录 (response.error / llm_error) 到本地日志
- [ ] 2.3 全量 ctest 零回归
      - `HYDRAFORGE_SKIP_REAL_LLM=1 ctest -j$(nproc)`
      - baseline 231 → 232 +1 new PASS, 0 regression
- [ ] 2.4 既有 mock cases 零回归 (N cases per test_skill_interpreter.cpp 既有)

## Phase 3 — commit + archive

- [ ] 3.1 git status 检查 (wrapper + test + tasks.md)
- [ ] 3.2 `git add include/agenticdsl/skill/skill_interpreter.h tests/test_skill_interpreter.cpp` + proposal/design/tasks/specs/README
- [ ] 3.3 commit `test(skill_interpreter): real-llm Phase E — dispatch_llm_generate host function` --no-verify
- [ ] 3.4 `openspec validate skill-interpreter-ipc-realllm --strict` exit 0
- [ ] 3.5 `tools/adr_lint.py` 0 errors
- [ ] 3.6 `tools/docs_drift_audit.py` 0 CRITICAL drift
- [ ] 3.7 archive (`openspec archive skill-interpreter-ipc-realllm`)

## E.5 GAP 记录 (本 change tasks.md)

- [ ] E.5.1 tasks.md §Phase E 末加 GAP 记录段 (含位置/现象/影响/归属/估时)
- [ ] E.5.2 NOT 修 `skill_interpreter.cpp:659` (本 change scope out, 属 Wave 4 fix-up)

## Tasks 总数

| Phase | Sub-tasks | New test cases | New API |
|---|---|---|---|
| 1 (wrapper + tests) | 3 | 1 | 1 (call_llm_generate_for_test) |
| 2 (验证) | 4 | — | — |
| 3 (commit + archive) | 7 | — | — |
| E.5 GAP 记录 | 2 | — | — |
| **Total** | **16 sub-tasks** | **1 new case** | **1 new wrapper** |

## 估时

| 阶段 | 估时 |
|---|---|
| 1 | 30 min (wrapper 简单 + 1 case) |
| 2 | 30 min (含真实 LLM 调试) |
| 3 | 15 min |
| **Total** | **~1.25 h** |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| host function direct test ≠ full IPC subprocess test (pipe/posix_spawn 未验证) | 测试覆盖路径 95% (host function 是 IPC handler 核心), E.2-E.4 follow-up 验证剩余 5% |
| call_llm_generate_for_test 误用为 production API | `[[deprecated("test only")]]` 标记 + 注释明确 + design.md §follow-up |
| model 遮蔽未修前实施 → 真实 LLM 必撞墙 | Wave 1 #1 已 ship 5 站点, 含 skill_interpreter.cpp:664 |
| SkillCapability allow_llm=false 阻止 dispatch | 测试显式设 allow_llm=true |
| dispatch_llm_generate 无超时 (E.5 GAP) | 本 change GAP 文档化, Wave 4 fix-up 跟踪 |
| call_llm_generate_for_test 触发 deprecation warning | 测试用 `[[deprecated]]` 抑制 (`#pragma GCC diagnostic ignored "-Wdeprecated-declarations"`), 或直接 invoke 不用 wrapper |

## follow-up (本 change archive 时记入)

1. **`skill-interpreter-ipc-e2e-realllm`**: E.2-E.4 full execute + SKILL.md fixture + 50KB pipe buffer
2. **`fix-skill-interpreter-token-and-timeout`** (Wave 4): dispatch_llm_generate 加 stop_token + 子进程优雅退出