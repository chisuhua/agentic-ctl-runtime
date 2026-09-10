# fix-skill-interpreter-token-and-timeout — Spec Delta

## Purpose

修复 `SkillInterpreter::run` 不接受 stop_token 参数, 让外部 cancel 信号可立即 SIGKILL 子进程 (不等到 cap.timeout_ms).

## ADDED Requirements

### Requirement: SkillInterpreter 接受 stop_token 参数

`SkillInterpreter::run` SHALL 接受 `std::stop_token token = {}` 形参, 默认空 token 保持向后兼容. `SkillInterpreter::Impl::run` SHALL 接受同形参并转发至 `ipc_loop_and_wait`. `ipc_loop_and_wait` SHALL 接受 `std::stop_token token = {}` 形参.

#### Scenario: 默认参数零行为变更
- WHEN 调用方以 2 参数调用 `interpreter.run(skill, cap)`
- THEN 默认 `token={}` 被使用, 与原无限等待行为不同但正确
- AND 现有调用方 (CognitiveWorker + 9 个 tests) 零回归

### Requirement: 外部 cancel 立即 SIGKILL

`ipc_loop_and_wait` 的 while 循环 SHALL 在每次迭代顶部检查 `token.stop_requested()`. 若 cancelled, SHALL 立即 `kill_retry(pid, SIGKILL)` + `waitpid_reap(pid, &status)` 并返回 `ErrorCode::Abort`, 不等 `cap.timeout_ms`.

#### Scenario: pre-cancel 立即 SIGKILL
- WHEN `token.stop_requested() == true` (pre-cancel)
- THEN 子进程被 SIGKILL + waitpid_reap
- AND `SkillResult.error_code == ErrorCode::Abort`
- AND elapsed time < 5s (不等到 30s default timeout)

### Requirement: 回归守卫

`tests/test_skill_interpreter.cpp` SHALL 包含 1 个新 case "7.8b pre-cancelled stop_token triggers immediate SIGKILL", 验证 pre-cancel 后 run() 在 < 5s 内返回 `ErrorCode::Abort`.

#### Scenario: 测试拦截回归
- WHEN 未来回退 while 循环的 token 检查
- THEN pre-cancel 测试中 `elapsed` 会达到 timeout_ms (30s)
- AND `CHECK(elapsed < 5000)` 断言失败 → 测试拦截