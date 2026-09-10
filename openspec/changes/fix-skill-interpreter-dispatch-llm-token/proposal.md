## Why

Wave 4 token passthrough Oracle bg_081db32f 交叉审查 follow-up #2. `SkillInterpreter::dispatch_llm_generate` (`src/modules/skill_interpreter/skill_interpreter.cpp:686`) 仍硬编码 `llm_->generate(gen_req, std::stop_token{})` — 父进程已有的外部 cancel token 在子进程发起 `llm_generate` IPC 时未透传至 LLM 调用. 父进程同步 generate 返回期间 (秒级甚至分钟级), 外部 cancel 完全失效. 这是 Wave 4 fix-skill-interpreter-token-and-timeout 仅修一半的残留.

## What Changes

**生产代码 (1 文件, 5 处签名同步)**:
- `src/modules/skill_interpreter/skill_interpreter.cpp:600` — `dispatch(req, cap, pid)` → `dispatch(req, cap, pid, std::stop_token token = {})`
- `src/modules/skill_interpreter/skill_interpreter.cpp:609` — `dispatch_llm_generate(req, cap)` → `dispatch_llm_generate(req, cap, token)`
- `src/modules/skill_interpreter/skill_interpreter.cpp:668` — `dispatch_llm_generate` 定义加 `std::stop_token token` 形参
- `src/modules/skill_interpreter/skill_interpreter.cpp:686` — THE FIX: `llm_->generate(gen_req, std::stop_token{})` → `llm_->generate(gen_req, token)`
- `ipc_loop_and_wait` 调用 dispatch 处 — 传入 `token` 形参

**测试 (1 文件, 1 新 case)**:
- `tests/test_skill_interpreter.cpp`: 新增 Test 7.8d "dispatch_llm_generate forwards external token"
  - 使用 RecordingLLMProvider (token-aware) 替代 MockToolRegistry
  - pre-cancel stop_token + skill 调用 llm_generate IPC
  - 断言 `last_token_stop_requested == true` (证明 token 透传至 llm_->generate)

## Capabilities

### New Capabilities
- (无 — 修改既有 capability)

### Modified Capabilities
- (使用既有 capability `skill-interpreter-real-loading` 或新增 `fix-skill-interpreter-token-and-timeout` delta spec)

## Impact

**API 影响**:
- 3 处函数签名加 `std::stop_token token = {}` 默认参数, 保证 100% 向后兼容 (现有 callers 零行为变更)
- 现有 9 个 tests 零回归

**测试影响**:
- 0 个 existing tests 需修改 (默认参数保留)
- 1 个 new test 新增

**风险**: 低 (默认 `{}` 参数 + 既有硬编码 `{}` 等价; 仅 llm_generate path 改变 token 来源)

## Non-goals

- **不修改** `dispatch_call_tool / dispatch_emit_event / dispatch_consume_budget` — 这些是同步快速调用, 无需 token 透传
- **不修改** 子进程侧 (skill_child_main) 的 IPCRequest 协议 — token 在父进程侧传递, 不跨进程边界
- **不新增** ErrorCode — 本 change 是 token 透传, 不是错误码语义 (follow-up #1 已统一)

## 升级触发

不适用 (此 change 是 Wave 4 follow-up, 非升级触发型).

## 估时

~30 分钟 (含 Oracle ship-gate).