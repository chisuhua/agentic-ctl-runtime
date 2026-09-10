# fix-skill-interpreter-token-and-timeout — Proposal

## Why

Wave 1 #1 (`fix-generation-request-model-default`) 修复 8 处 model 遮蔽时, 未涉及 SkillInterpreter 的 stop_token 透传。`SkillInterpreter::run()` 是长 turn 操作的入口 (默认 timeout 30s), 没有 stop_token 透传意味着外部 cancel 信号 (Steering 中断/Agent 关闭/父进程退出) 无法立即生效, 必须等满 timeout 才能 SIGKILL 子进程。

## Scope

**In scope**:
- `SkillInterpreter::run` 加 `std::stop_token token = {}` 形参 (默认 `{}` 向后兼容)
- `SkillInterpreter::Impl::run` 加同形参, 转发至 `ipc_loop_and_wait`
- `ipc_loop_and_wait` 加同形参, while loop 顶部检查 `token.stop_requested()` → SIGKILL + 返回 `ErrorCode::Abort`
- 1 个新测试 (Test 7.8b) 验证 pre-cancel SIGKILL 在 < 5s 内完成

**Out of scope**:
- 子进程侧 (skill_child_main) 接受 token — 当前子进程是 IPC pull-based, 父进程 SIGKILL 是更可靠的取消手段
- CognitiveWorker 接入 stop_token — 属 fix-orchestrator-caller-token 后续 fix-up

## Impact

**API 影响**: run 多 1 形参, 默认 `= {}` 保证 100% 向后兼容

**测试影响**: 0 个 tests 需修改; 1 个 test 新增

## 估时

~45 分钟 (含 Oracle ship-gate)。