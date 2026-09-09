# cost-tracking-decorator-realllm

**Status**: Draft (scaffold)

## Scope

`real-llm-core-coverage` Phase D 实施: `CostTrackingDecorator` 真实 deepseek LLM
端到端计费验证，3 cases 新增 `tests/test_cost_tracking_decorator.cpp`。

## Why

CostTrackingDecorator 是 Sprint 5 ship 的 Phase 5 Budget Hole 修复（REQ-IPD-002），
核心契约：decorated generate → completion_tokens > 0 → `budget_->record_llm_call`
扣费 > 0。既有 4 cases 用 `MockLLMProvider` + `MockBudget` 验证合约单元，**未在真实
LLM 下验证**：
- completion_tokens 真实返回值（mock 写死 5）
- budget 扣费金额真实计算（token 数 × cost_per_token_for）
- 流式 TrackingStream 析构兜底计费路径

## 验证

- `openspec validate cost-tracking-decorator-realllm --strict` exit 0
- `ctest -j$(nproc)` baseline 228 → 231 +3 new, 0 regression
- skip 模式 SUCCEED；真实 deepseek 至少 1 case PASS（本地有 key）

## 依赖

- **依赖**: `real-llm-core-coverage` Phase 0+A 已 ship（helper 可用）
- **依赖**: Wave 1 #1 `fix-generation-request-model-default` 已 ship（req.params.model.clear() 已应用于 8 站点，含 cost_tracking_decorator.cpp:39 读 model 路径）
- **被依赖**: `real-llm-core-coverage` Phase D ship → Phase E/G 启用真实 LLM 时 cost 验证就绪

## Artifacts

- `proposal.md` — Why / Scope / Impact / 依赖 / 升级触发 / 估时
- `design.md` — 测试架构 + helper 复用 + CostTrackingDecorator API + 3 cases 模板
- `tasks.md` — 13 sub-tasks across 3 phases
- `specs/cost-tracking-decorator-realllm/spec.md` — 4 ADDED Requirements