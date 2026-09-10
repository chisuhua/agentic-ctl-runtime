# toolresult Specification

## Purpose
Phase 1 入口 ToolResult 标准化(ADR-0023) — 当前工具调用结果格式全线不一致:`ToolRegistry::call_tool()` 多格式 (`{"result": 42}` / `{"error": "..."}`) + `NodeExecutor::execute_tool_call()` 启发式分支 `if(result.is_object())` 脆弱 + 4+ 默认工具各用各的顶层 key (`results` / `result` / `location`) + ADR-0021 `RETURN_SUCCESS` 格式未定义 (PDK 无输出合约) + ADR-0019 `Event.content` `std::string` 结构化数据丢失;Phase 0 X 阶段已交付 ToolResult MVP (24/24 测试),本 change 扩展 P1→P4:**P2** 错误码分类(`ErrorCode::RETRY`/`SKIP`/`ABORT`)、**P3** 调用元数据(`latency_ms`/`trace_id`/`caller`)、**P4** `IInteractionBus` 结构化推送集成(`tool.completed` 事件携带 4 字段)。
## Requirements
### Requirement: NodeExecutor 结构化解析 (REQ-TR-MOD-001)

`NodeExecutor::execute_tool_call` **MUST** 不再使用 `if(result.is_object())` 启发式判断成功/失败。

**原** (`src/modules/executor/node_executor.cpp:execute_tool_call`):
```cpp
if (result.is_object()) { /* success */ }
```

**新**:
```cpp
// 信封模式：解析 ok + 4 个 P2-P4 字段
// 旧式裸 JSON 模式：包装为 success 信封保留 P0 行为
ToolResult tool_result;
if (raw_result.is_object() && raw_result.contains("ok")
    && raw_result["ok"].is_boolean()) {
  tool_result = ToolResult::from_json(raw_result);
} else {
  tool_result = ToolResult::success(raw_result);
}
// 自动注入 latency_ms / trace_id
// error_code 分发 Retry/Abort/Skip
```

#### Scenario: 工具结果结构化解析

- **WHEN** NodeExecutor 调用 `ToolRegistry::call_tool()` 返回原始 JSON
- **THEN** 调用 `ToolResult::from_json()` 解析信封或包装裸 JSON 为 success
- **AND** 不再使用 `is_object()` 等启发式判断成功/失败

### Requirement: ErrorCode 新增 Cancelled 值

ErrorCode enum SHALL 新增 `Cancelled` 值, 数值紧接 `InvalidParams` 之后 (保持 enum 末尾追加). 该值 SHALL 表示 LLM 消费侧 stop_token 触发的取消语义, 与 `Abort` (终止整个流程) 区分.

#### Scenario: 默认数值向后兼容
- WHEN ErrorCode 枚举值顺序变更
- THEN 既有 18 个值 (`Unknown` 到 `InvalidParams`) 数值不变
- AND 新值 `Cancelled` 是第 19 个值
- AND 任何依赖 enum 数值的序列化 (JSON, 网络协议) 保持兼容

### Requirement: Cancelled string 映射双向对称

`error_code_to_string()` 和 `string_to_error_code()` SHALL 对 `ErrorCode::Cancelled` 提供双向对称映射. 即 `error_code_to_string(ErrorCode::Cancelled) == "Cancelled"` 且 `string_to_error_code("Cancelled") == ErrorCode::Cancelled`.

#### Scenario: 双向对称 round-trip
- WHEN 调用 `error_code_to_string(ErrorCode::Cancelled)`
- THEN 返回字符串 `"Cancelled"`
- AND 调用 `string_to_error_code("Cancelled")` 反向解析
- AND 返回 `ErrorCode::Cancelled`

### Requirement: LLMError::Cancelled 映射到 ErrorCode::Cancelled

`llm_error_to_error_code()` SHALL 将 `LLMError::Code::Cancelled` 映射到 `ErrorCode::Cancelled` (而非 `ErrorCode::Unknown`). 既有 `LLMError::Code::NetworkError/RateLimited/ServerError → ErrorCode::Retry` + `LLMError::Code::AuthenticationError → ErrorCode::PermissionDenied` + `LLMError::Code::ContextOverflow → ErrorCode::ResourceExhausted` + `LLMError::Code::InvalidRequest → ErrorCode::Unknown` + `LLMError::Code::Unknown → ErrorCode::Unknown` 保持不变.

#### Scenario: Cancelled 触发 ErrorCode::Cancelled
- WHEN provider 返回 `Result<GenerationResult, LLMError>::failure(LLMError{Code::Cancelled, ...})`
- AND SimpleCognitiveOrchestrator::react_once 调用 `llm_->generate(req, token)`
- AND token.stop_requested() == true 触发 LLM 内部取消
- THEN `llm_error_to_error_code()` 返回 `ErrorCode::Cancelled`
- AND ToolResult.error_code == ErrorCode::Cancelled (而非 ErrorCode::Unknown)

