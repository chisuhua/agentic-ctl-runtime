# fix-orchestrator-token-passthrough — Spec Delta

## Purpose

修复 `SimpleCognitiveOrchestrator` 硬编码 `std::stop_token{}` (simple_orchestrator.cpp:125), 让外部 cancel 信号可透传至 LLM provider.

## ADDED Requirements

### Requirement: SimpleCognitiveOrchestrator 接受 stop_token 参数

`SimpleCognitiveOrchestrator::process` SHALL 接受 `std::stop_token token = {}` 形参, 默认空 token 保持向后兼容. `SimpleCognitiveOrchestrator::react_once` SHALL 接受 `std::stop_token token = {}` 形参, 默认空 token 保持向后兼容.

#### Scenario: 默认参数零行为变更
- WHEN 调用方以 2 参数调用 process(session_id, cb)
- THEN 默认 token={} 被使用, 与原硬编码 `llm_->generate(req, {})` 完全等价
- AND 现有调用方 (CognitiveWorker + 7 个 tests + 1 example) 零回归

#### Scenario: token 透传至 provider
- WHEN 调用方以 3 参数调用 process(session_id, cb, ss.get_token())
- THEN process 转发 token 至 react_once
- AND react_once 转发 token 至 llm_->generate
- AND RecordingLLMProvider 的 last_token_stop_requested == token.stop_requested()

### Requirement: 回归守卫

`tests/test_simple_orchestrator.cpp` SHALL 包含 1 个新 case "SimpleCognitiveOrchestrator forwards stop_token to LLM provider", pre-cancel stop_token, 验证 RecordingLLMProvider::last_token_stop_requested == true.

#### Scenario: 测试拦截回归
- WHEN 未来回退 `react_once` 至 `llm_->generate(req, {})`
- THEN RecordingLLMProvider 收到 `{}` token → last_token_stop_requested == false
- AND 新 case 断言失败 → 测试拦截
