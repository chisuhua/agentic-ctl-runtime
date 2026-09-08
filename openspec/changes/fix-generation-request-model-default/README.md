# fix-generation-request-model-default

**Status**: Draft (scaffold)

## Scope

系统性修复 `LLMParams` 默认 `model="gpt-4o-mini"` 遮蔽 CloudLLMAdapter 配置真实
model 的 bug 在 5 个潜伏站点（+ 1 个已 ship by `real-llm-core-coverage` Phase A）。

## Why

`real-llm-core-coverage` Phase A 实施时发现并修复 orchestrator 一个站点。
Oracle 独立审查（`ses_f7f5ef175ffeGKhxXLfBJjzLVX`）实证 5 个同类潜伏站点:

| 站点 | 文件 | 行号 |
|---|---|---|
| GenerateSubgraphNode ll_call | `src/modules/executor/node_executor.cpp` | 356 |
| YieldNode 流式 | `src/modules/executor/node_executor.cpp` | 574 |
| Skill IPC llm_generate | `src/modules/skill_interpreter/skill_interpreter.cpp` | 657-659 |
| ContextCompactor 摘要 | `src/core/context_compactor.cpp` | 60 |
| GEPA reflection | `src/modules/cognitive/gepa_loop.cpp` | 115 |

每站点加 `req.params.model.clear()` 让 CloudLLMAdapter L164 正确 fallback
`config_.model`。

## 验证

- `openspec validate fix-generation-request-model-default --strict` exit 0
- `ctest -j$(nproc)` baseline 224 → 229 +5 new, 0 regression
- 无 API key 时新测试 PASS (CI 友好, 不依赖真实 LLM)

## 依赖

- **依赖**: `real-llm-core-coverage` Phase A 已 ship orchestrator 修复
- **被依赖**: `real-llm-core-coverage` Phase E (Skill IPC) + Phase G (ContextCompactor) +
  Phase F (YieldNode 部分) 需要本 change 启用真实 LLM

## 升级触发

若 Phase C-G 暴露 ≥3 个站点需同类修复 → 立即纳入本 change (追加 commit),
不再是 deferred 状态.

## Artifacts

- `proposal.md` — Why / What / Scope / Impact / 验证 / 估时
- `design.md` — 5 站点修复位置 + 测试设计 + telemetry 副作用
- `tasks.md` — 18 sub-tasks across 3 phases
- `specs/fix-generation-request-model-default/spec.md` — 4 ADDED Requirements
