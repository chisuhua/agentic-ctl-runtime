## ADDED Requirements

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