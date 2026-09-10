# fix-skill-interpreter-token-and-timeout

**Status**: Draft (scaffold)

## Scope

Wave 4 fix-up change. `SkillInterpreter::run` 不接受 stop_token 参数 → 外部 cancel 信号无法立即 SIGKILL 子进程，需等满 cap.timeout_ms (默认 30s) 才能终止。

## 改动

**生产代码 (2 文件)**:
- `include/agenticdsl/skill/skill_interpreter.h:81` — `run(skill_path, cap)` → `run(skill_path, cap, std::stop_token = {})`
- `src/modules/skill_interpreter/skill_interpreter.cpp:179` — `SkillInterpreter::run` 接受 token, 转发至 `impl_->run`
- `src/modules/skill_interpreter/skill_interpreter.cpp:355` — `ipc_loop_and_wait` 加 `std::stop_token token = {}` 形参
- `src/modules/skill_interpreter/skill_interpreter.cpp:380-396` — NEW: while loop 顶部 `if (token.stop_requested()) { kill_retry(pid, SIGKILL); return ErrorCode::Abort; }`
- `src/modules/skill_interpreter/skill_interpreter.cpp:726` — public wrapper 转发 token

**测试 (1 文件)**:
- `tests/test_skill_interpreter.cpp` 新增 Test 7.8b "pre-cancelled stop_token triggers immediate SIGKILL"

## 依赖

无前置。可与 fix-yield-node-token-passthrough + fix-orchestrator-token-passthrough 并行。

## 升级触发

不适用。

## 估时

~45 分钟 (含 Oracle ship-gate)。