## Context

Wave 4 token passthrough Oracle bg_081db32f 交叉审查发现 Cancel 错误码语义模糊:
- LLM 侧: `LLMError::Code::Cancelled` (src/common/llm/llm_types.h) — 表达 stop_token 取消
- ToolResult 侧: 通过 `llm_error_to_error_code()` (simple_orchestrator.cpp:36-39) 映射到 `ErrorCode::Unknown` — 语义丢失, 与"真正未知错误"无法区分
- SkillInterpreter 侧 (Wave 4 ship): SIGKILL-on-cancel 返回 `ErrorCode::Abort` — 语义为"终止整个流程"

3 个 layer (LLMError / ErrorCode / SkillResult.error_code) 对同一概念 (cancel) 使用 3 个不同 code (`Cancelled` / `Unknown` / `Abort`). 调用方无法可靠区分 cancel vs other failure.

## Goals / Non-Goals

**Goals**:
- ErrorCode enum 新增 `Cancelled` 值
- `llm_error_to_error_code()` 将 `LLMError::Cancelled` 映射到 `ErrorCode::Cancelled`
- string 映射双向对称
- 1 个新测试验证 RecordingLLMProvider 触发 Cancelled → ToolResult.error_code == ErrorCode::Cancelled

**Non-Goals**:
- 不修改 `ErrorCode::Abort` 在 skill_interpreter.cpp:392 的使用 (不同语义层 — abort skill execution vs cancel LLM call)
- 不修改 cognitive_worker.cpp:60 的旧 `ERR_LLM.CANCELLED` string mapping (已 enum 化, 独立 follow-up)
- 不修改 LLMError::Code enum 或其定义 (LLMError 是独立 enum, 不在 ErrorCode 体系内)
- 不引入"统一跨层 cancel 语义" — 保持 ErrorCode::Cancelled 专用于 ErrorCode 层

## Decisions

### Decision 1: ErrorCode 末尾追加 `Cancelled` (而非插入中间)

**选择**: 在 `InvalidParams` 后追加 `Cancelled = InvalidParams + 1`.

**理由**:
- enum 数值向后兼容 (既有 18 个值不变, 任何依赖数值序列化的代码零变更)
- 与之前 P3 (skill-interpreter-real-loading 2026-07-21) + ADR-0073 D3 (InvalidParams 2026-08-18) 的追加策略一致

**Alternatives considered**:
- 插入 enum 中间 (如 `Cancelled` 在 `Abort` 之后): 会改变既有 enum 数值, 影响 JSON 序列化兼容性 — 拒绝
- 复用 `ErrorCode::Abort` 而不新增 `Cancelled`: 语义混淆 (abort skill vs cancel LLM), 与 Oracle follow-up 设计初衷相悖 — 拒绝

### Decision 2: `llm_error_to_error_code()` 单独修改 `Cancelled` case

**选择**: 修改 `case LLMError::Code::Cancelled` 从 `return ErrorCode::Unknown` 改为 `return ErrorCode::Cancelled`. 其他 case 保持不变.

**理由**:
- 单点修改, 影响面最小
- 其他 `LLMError::Code::InvalidRequest/Unknown → ErrorCode::Unknown` 保持原样 (这两个语义确为"未明确错误")
- `LLMError::Cancelled` 现在有专属 ErrorCode 映射, 调用方可可靠区分

**Alternatives considered**:
- 同时修改 `LLMError::Code::InvalidRequest` 映射: 不在本 change scope (Oracle 观察未涉及此 case)
- 重构整个映射表为表驱动: 过度工程, 当前 switch 已清晰可读 — 拒绝

### Decision 3: string 映射双向对称 (与现有 18 个值一致)

**选择**: `error_code_to_string(ErrorCode::Cancelled) == "Cancelled"`, `string_to_error_code("Cancelled") == ErrorCode::Cancelled`.

**理由**:
- 现有 18 个值 (tool_result.cpp:29, 53) 都有 string 映射双向对称, 新值必须遵循同一模式
- 序列化 (JSON 输出 ToolResult.error_code + meta.error_message) 需要双向可还原

**Alternatives considered**:
- 省略 string 映射: ToolResult JSON 序列化会出现数字 18 而非 "Cancelled", 不可读 — 拒绝
- 自定义 string (如 "OperationCancelled"): 与现有命名风格不一致 — 拒绝

## Risks / Trade-offs

[Risk] switch case 未覆盖新 enum 值 → 编译期 -Wswitch warning
→ Mitigation: 修改 enum 时同步检查所有 switch consumer (tool_result.cpp / cognitive_worker.cpp / node_executor.cpp / test files); 验证 `cmake --build` 零 warning.

[Risk] 旧 ToolResult JSON 序列化中 ErrorCode::Cancelled 数值是新 18, 旧版本无法解析
→ Mitigation: 数值 18 是新值, 旧版本从未生成过, 仅向前 (new producer → new consumer). 现有 18 个值序列化兼容.

[Risk] 调用方依赖 `error_code == ErrorCode::Unknown` 隐式判定 cancel (如 test_simple_orchestrator.cpp Test 7)
→ Mitigation: Test 7 当前用 pre-cancel + RecordingLLMProvider (token-aware), 不依赖 Unknown. Test 8 新增显式 Cancelled 验证.

## Migration Plan

不适用. 纯前向兼容 (新值追加). 无部署/回滚需求.

## Open Questions

无. 所有决策已明确.