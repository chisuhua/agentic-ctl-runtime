## ADDED Requirements

### Requirement: dispatch_llm_generate 接受 stop_token 参数

`SkillInterpreter::Impl::dispatch_llm_generate` SHALL 接受 `std::stop_token token = {}` 形参, 默认空 token 保持向后兼容. 该 token SHALL 在调用 `llm_->generate()` 时透传, 替换既有硬编码 `std::stop_token{}`.

#### Scenario: 默认参数零行为变更
- WHEN 调用方以 2 参数调用 `dispatch_llm_generate(req, cap)`
- THEN 默认 `token={}` 被使用, 与原硬编码 `{}` 完全等价
- AND 现有 9 个 callers 零回归

#### Scenario: token 透传至 llm_->generate
- WHEN 调用方以 3 参数调用 `dispatch_llm_generate(req, cap, ss.get_token())`
- AND `ss.get_token().stop_requested() == true`
- THEN `llm_->generate(gen_req, token)` 收到 cancelled token
- AND RecordingLLMProvider 的 `last_token_stop_requested == true`

### Requirement: dispatch 接受 stop_token 参数并透传至 dispatch_llm_generate

`SkillInterpreter::Impl::dispatch` SHALL 接受 `std::stop_token token = {}` 形参. 当 `req.method == "llm_generate"` 时, dispatch SHALL 调用 `dispatch_llm_generate(req, cap, token)` 透传 token. 其他 dispatch_* 函数 SHALL 不需要 token 透传.

#### Scenario: llm_generate path 透传
- WHEN 子进程通过 IPC 发送 `llm_generate` 请求
- AND ipc_loop_and_wait 收到请求
- AND ipc_loop_and_wait 持有外部 cancel token
- THEN dispatch 把 token 透传至 dispatch_llm_generate
- AND dispatch_llm_generate 把 token 透传至 llm_->generate

### Requirement: 回归守卫

`tests/test_skill_interpreter.cpp` SHALL 包含 1 个新 case "7.8d dispatch_llm_generate forwards external stop_token". pre-cancel stop_token + 子进程调用 llm_generate IPC, 断言 `RecordingLLMProvider::last_token_stop_requested == true`.

#### Scenario: 测试拦截回归
- WHEN 未来回退 `dispatch_llm_generate` 中 `llm_->generate(gen_req, std::stop_token{})` 硬编码
- THEN RecordingLLMProvider 收到 `{}` token → `last_token_stop_requested == false`
- AND 新 case 断言失败 → 测试拦截