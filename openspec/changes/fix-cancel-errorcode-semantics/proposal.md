## Why

Wave 4 token passthrough Oracle 交叉审查 (bg_081db32f) 发现 Cancel 错误码语义模糊:
- `LLMError::Code::Cancelled` → `ErrorCode::Unknown` (simple_orchestrator.cpp:36-39)
- `ErrorCode::Abort` 用于 skill_interpreter SIGKILL-on-cancel 路径 (skill_interpreter.cpp:392)

同一"cancel"语义在 LLM 侧产生 `LLMError::Cancelled`, 但映射成 `ErrorCode::Unknown` 而非专属码. 调用方无法区分"cancel 触发的失败"vs"未知的失败". `ErrorCode::Abort` 虽存在但语义为"终止整个流程", 与 cancel 不严格等价. 建议统一语义.

## What Changes

- **`src/core/types/tool_result.h`**: ErrorCode enum 新增 `Cancelled` 值 (P4, 紧接 `InvalidParams` 之后, 保持 enum 数值连续)
- **`src/core/types/tool_result.cpp`**: 
  - `error_code_to_string(ErrorCode::Cancelled)` → `"Cancelled"`
  - `string_to_error_code("Cancelled")` → `ErrorCode::Cancelled`
- **`src/modules/cognitive/simple_orchestrator.cpp`**: `llm_error_to_error_code()` 修改 `LLMError::Code::Cancelled` case 从 `ErrorCode::Unknown` 改为 `ErrorCode::Cancelled`
- **`tests/test_simple_orchestrator.cpp`**: 新增 Test 8 — `LLMError::Code::Cancelled` 透传至 `ErrorCode::Cancelled` (RecordingLLMProvider 触发 Cancelled, 断言 ToolResult.error_code)
- **`tests/test_tool_result_strings.cpp`** (新文件, 若不存在): 验证 `error_code_to_string` + `string_to_error_code` 对 `Cancelled` 双向对称 (无值偏移)

## Capabilities

### New Capabilities
<!-- 无新增独立 capability, 修改既有 toolresult spec -->
- (无)

### Modified Capabilities
- `toolresult`: ErrorCode enum 新增 `Cancelled` 值 + 对应 string 映射双向对称 + `llm_error_to_error_code` 将 `LLMError::Cancelled` 映射到新值

## Impact

**代码影响**:
- `src/core/types/tool_result.h`: enum +1 value (影响所有 switch consumer, 编译期 warning 必须为 0)
- `src/core/types/tool_result.cpp`: 2 处 string mapping 双向对称
- `src/modules/cognitive/simple_orchestrator.cpp`: 1 处 case 修改
- 2 个 test files: 新增 1 case + 可能的新文件

**API 兼容**:
- ErrorCode 数值向后兼容 (新值追加在末尾, 既有 18 个值不变). 已有的 ToolResult.error_code 序列化 (JSON 输出) 不变. 字符串映射双向对称.

**测试影响**:
- 0 个 existing test 需修改 (新值追加)
- 1 个 new test 新增 (Test 8 Cancelled 透传)
- 全量 ctest 228/228 应保持零回归

**Non-goals**:
- **不修改** skill_interpreter.cpp:392 使用 `ErrorCode::Abort` 的语义 — "abort skill execution" 与 "cancel LLM call" 是不同语义层, 不强行统一. `ErrorCode::Abort` 保留用于 skill 全流程终止, `ErrorCode::Cancelled` 专用于 LLM 消费侧 stop_token 取消.
- **不修改** cognitive_worker.cpp:60 (`ERR_LLM.CANCELLED` string mapping 到 `ErrorCode::Abort`) — 这是旧 string 协议, 已被 enum 替代, 不在本 change scope.

## 升级触发

不适用 (无外部触发条件).

## 估时

~1 小时 (含 Oracle ship-gate 复核).