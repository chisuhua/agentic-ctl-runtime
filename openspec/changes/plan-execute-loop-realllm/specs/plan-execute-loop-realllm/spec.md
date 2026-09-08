# Spec: plan-execute-loop-realllm

## Purpose

为 `PlanExecuteLoop` 三阶段循环（Plan → Execute → Verify）建立生产级真实 LLM
测试覆盖。`plan_phase` 和 `verify_phase` 是 `LLMParams` 默认遮蔽的 5 个潜伏面
中已有完整调用栈的两个 — 本 change 验证真实 deepseek 下链路工作，同时作为
`fix-generation-request-model-default` ship 后的端到端验证用例。

## ADDED Requirements

### Requirement: plan_phase 真实 LLM 产出可解析 DSL

`PlanExecuteLoop::plan_phase` 真实 deepseek LLM SHALL 产出含 AgenticDSL markdown 的响应, 且 `DSLEngine::continue_with_generated_dsl` SHALL 能成功解析 (不抛异常). 解析成功后 `result.final_context.working["meta"]["plan_appended"]` SHALL 为 `true`.

#### Scenario: PlanExecuteLoop plan_phase produces parseable DSL with real LLM
- GIVEN real deepseek configured
- AND PlanExecuteLoop 构造 (engine + 真实 provider 传入)
- WHEN run("compute 2+3", ctx) 触发 plan_phase
- THEN plan_phase LLM 输出含 AgenticDSL markdown
- AND `DSLEngine::continue_with_generated_dsl(dsl)` 解析成功 (不抛异常)
- AND `result.final_context.working["meta"]["plan_appended"] == true`

### Requirement: verify_phase 真实 LLM 含 "yes"

`PlanExecuteLoop::verify_phase` 真实 deepseek LLM SHALL 响应包含 "yes" (大小写不敏感 substring 匹配), verify_phase SHALL 返回 `true`, loop SHALL 终止成功 (`result.success == true`).

#### Scenario: PlanExecuteLoop verify_phase responds "yes" with real LLM
- GIVEN real deepseek configured
- AND mock pre-fills plan_phase 响应 (合法 DSL, mock 路径)
- AND verify_phase 走真实 LLM
- WHEN run() 触发 verify_phase
- THEN LLM 响应文本经 lowercase + substring 包含 "yes"
- AND verify_phase 返回 true
- AND `result.success == true` (loop 成功完成)

#### Scenario: PlanExecuteLoop verify_phase case-insensitive match
- GIVEN verify_phase LLM 响应可能是 "yes" / "Yes" / "YES" / "Yes, success" 等
- WHEN run() 触发 verify_phase
- THEN 大小写不敏感 substring 匹配覆盖所有常见变体
- AND result.success 反映 verify 判定

### Requirement: end-to-end 真实 LLM 全链路

`PlanExecuteLoop::run` 真实 deepseek SHALL 完成三阶段 (plan → execute → verify) 全链路, 最终 `result.success == true`, `result.message == "PlanExecuteLoop: completed successfully"`. 多次串行 run SHALL 不 panic, `retries_used <= 3`.

#### Scenario: PlanExecuteLoop end-to-end run("compute 2+3") real LLM
- GIVEN real deepseek configured
- AND PlanExecuteLoop 构造 (engine + 真实 provider 传入)
- WHEN run("compute 2+3", ctx) 完整三阶段
- THEN plan_phase 产出可解析 DSL
- AND execute_phase 追加 DSL 到 engine
- AND verify_phase LLM 响应含 "yes"
- AND `result.success == true`, `result.message == "PlanExecuteLoop: completed successfully"`

#### Scenario: End-to-end graceful on LLM flake (鲁棒性)
- GIVEN 真实 LLM 输出不可控 (plan DSL 可能格式瑕疵, verify 可能 "no")
- WHEN 多次 run 串行执行
- THEN 系统不 panic, result.message 有意义
- AND retries_used ≤ 3 (PlanExecuteLoop 上限)

### Requirement: scope 边界修正 (scope 扩展记录)

**Scope 修正 (Oracle ship-gate ses_f7cdf267bffeddRNuPpIcdOZd0)**:
原 proposal/spec 假设 Wave 1 #1 已 ship `plan_execute_loop.h` 的 2 站点 clear(),
但 Wave 1 #1 Oracle 独立审查 (ses_f7f5ef175ffeGKhxXLfBJjzLVX) 实证 5 个站点时遗漏
`include/agenticdsl/pdk/agent_loops/plan_execute_loop.h:208` (plan_phase) + `:254`
(verify_phase), ship commit 仅覆盖 4 个文件 (node_executor / skill_interpreter /
context_compactor / gepa_loop).

本 change 实施时发现遗漏, 立即扩展 scope 补 2 站点 clear() (per AGENTS.md
模式 #1 test-driven bug discovery closed loop step #2). 关闭 "TDD 发现 → 修复 →
守卫 → 测试" 闭环. 拆独立 change 反而会让测试先对着已知坏路径写 (真实 deepseek
必撞 server 拒绝 "you passed gpt-4o-mini").

**修正后的 scope 边界**:

#### Scenario: 2 站点 model 遮蔽 SHALL be fixed in this change
- GIVEN plan_execute_loop.h:208 (plan_phase) + :254 (verify_phase) 是 Oracle
  漏掉的 LLMParams 默认遮蔽潜伏面
- WHEN 本 change 实施
- THEN 加 `req.params.model.clear()` + AGENTS.md 模式 #1 "NOT redundant" 注释
- AND 现有 spec.md §Scope 边界 (Out) 的 "❌ 不修改 PlanExecuteLoop production code"
  SHALL 被本 requirement 覆盖 (scope 扩展决策已记录)

#### Scenario: verify_phase 判定逻辑 ("yes" substring) SHALL NOT be changed
- 理由: 现有契约保持稳定, 大小写不敏感 substring 是 ship 契约
- AND prompt 设计改进属 prompt-engineering follow-up, 不在本 change

#### Scenario: Token passthrough SHALL NOT be in this change
- 理由: plan_phase / verify_phase 已正确传 `token` 参数 (line 217/268)
- AND 多线程 stop_token 透传断裂属独立 fix-up changes

#### Scenario: Multi-thread PlanExecuteLoop SHALL NOT be in this change
- 理由: PlanExecuteLoop 单线程 run(), 多线程 risk 不适用
- AND 即使 SerializingDecorator 启用, 不影响此 change (单线程 path)

## 验证标准

- `cmake --build build -j$(nproc)` 编译通过 (0 error, 0 warning)
- skip 模式: 3 cases SUCCEED short-circuit (CI 友好)
- 真实 deepseek: 至少 1 case PASS (本地有 key 验证)
- 全量 `ctest -j$(nproc)` baseline 224 → 227 +3 PASS, 0 regression
- `tests/test_plan_execute_restart.cpp` 零回归
- `openspec validate plan-execute-loop-realllm --strict` exit 0
- `tools/adr_lint.py` 0 errors
- `tools/docs_drift_audit.py` 0 CRITICAL drift

## 依赖

| 上游 | 状态 | 影响 |
|---|---|---|
| `real-llm-core-coverage` Phase 0+A | ✅ SHIPPED | helper 自测已可用 |
| `fix-generation-request-model-default` | ⏳ PENDING P0 | **硬依赖**: model 遮蔽未修, plan/verify 真实 LLM 必撞墙 |

| 下游 | 内容 |
|---|---|
| `real-llm-core-coverage` Phase D (Cost) | verify_phase 真实 LLM 路径成本验证 |
| PDK Agent 用户 (loop_agent plugin) | PlanExecuteLoop 可用真实 deepseek |

## References

- **设计依据**: `include/agenticdsl/pdk/agent_loops/plan_execute_loop.h:208` (plan_phase)
  + `:254` (verify_phase) — 5 个 model 遮蔽潜伏面中 2 个
- **关联修复**: `fix-generation-request-model-default` (硬依赖)
- **测试模式**: `real-llm-core-coverage` Phase A A.2 (CognitiveWorker 真实 LLM) +
  Phase B B.4 (mock simulate RateLimited) 复用
- **既有测试**: `tests/test_plan_execute_restart.cpp` (Sprint 20 mock 测试, 不修改)