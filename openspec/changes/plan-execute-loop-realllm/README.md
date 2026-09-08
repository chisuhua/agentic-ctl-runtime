# plan-execute-loop-realllm

**Status**: Draft (scaffold)

## Scope

`real-llm-core-coverage` Phase C 实施: `PlanExecuteLoop` 三阶段循环（Plan → Execute
→ Verify）真实 deepseek LLM 端到端测试覆盖，3 cases 新建 `tests/test_plan_execute_realllm.cpp`。

## Why

PlanExecuteLoop 是 PDK Agent 编排核心。`plan_phase` 和 `verify_phase` 是
`LLMParams` 默认遮蔽 5 个潜伏面中**已有完整调用栈**的两个 — 真实 deepseek 下
未验证：
- `plan_phase` 产出**合法可解析 DSL**?
- `verify_phase` 响应**含 "yes"** → 循环终止?

## 验证

- `openspec validate plan-execute-loop-realllm --strict` exit 0
- `ctest -j$(nproc)` baseline 224 → 227 +3 new, 0 regression
- skip 模式 SUCCEED; 真实 deepseek 至少 1 case PASS (本地有 key)
- 既有 `test_plan_execute_restart.cpp` 零回归

## 依赖

- **硬依赖**: `fix-generation-request-model-default` 必须先 ship — plan_phase /
  verify_phase 是 5 个潜伏面之一, model 遮蔽未修则真实 LLM 必撞墙
- **前置**: `real-llm-core-coverage` Phase 0+A 已 ship (helper 可用)

## Artifacts

- `proposal.md` — Why / Scope / Impact / 依赖 / 升级触发 / 估时
- `design.md` — 测试架构 + helper 复用 + model workaround + 3 cases 模板
- `tasks.md` — 13 sub-tasks across 3 phases
- `specs/plan-execute-loop-realllm/spec.md` — 4 ADDED Requirements