## Why

`real-llm-core-coverage` Phase A 实施时（commit `afc2d1b`）发现并修复了一个 LLMParams
默认值遮蔽生产 model 的 bug：`SimpleCognitiveOrchestrator::react_once` 构造
`GenerationRequest` 不显式设 `params.model`，由于 `LLMParams = LLMConfig` 别名 +
`LLMConfig::model` 默认 `"gpt-4o-mini"`（非空），CloudLLMAdapter L164
`req.params.model.empty() ? config_.model : req.params.model` 拿默认值遮蔽 adapter
配置的 `deepseek-v4-flash` → deepseek server 拒绝 → 真实 LLM 测试 FAIL。

**该修复仅覆盖 orchestrator 一个站点**。Oracle 审查（session
`ses_f7f5ef175ffeGKhxXLfBJjzLVX`）独立验证了 **5 个同类潜伏站点** 都构造
`GenerationRequest` 不设 `params.model`：

- `node_executor.cpp:356` — `GenerateSubgraphNode` ll_call 路径（`req` 默认 params）
- `node_executor.cpp:574` — `YieldNode` `generate_stream(req, {})`（同 356）
- `skill_interpreter.cpp:657-659` — IPC `llm_generate`（`gen_req.prompt = prompt; auto result = llm_->generate(gen_req, ...)`）
- `src/core/context_compactor.cpp:60` — `ContextCompactor::compact` 摘要
- `src/modules/cognitive/gepa_loop.cpp:115` — GEPA 反射
  (`GenerationRequest request("Reflect on failed execution " + ...)`)

**Phase E (Skill IPC) / G (ContextCompactor)** 启用真实 LLM 时**不经过 orchestrator**，
`react_once` 的 `clear()` 救不了它们 → 预期以 "you passed gpt-4o-mini" 失败（环境问题
误判风险 + 反复调试时间）。

用户原始关注（`real-llm-core-coverage` proposal 引文）："交了 LLM 后延时会变长，需要测试来
验证能工作" — 系统性修复是 phase E/G 真实 LLM 测试的前置条件。

## What Changes

### Scope: production code 最小修复（5 站点）

每个潜伏站点加 `req.params.model.clear()`（或在该站点显式不设 model），让
`CloudLLMAdapter::build_request_body` L164 正确 fallback 到 `config_.model`（adapter
构造时 LLMProviderFactory 设置的真实 model，例如 `deepseek-v4-flash`）。

**不修改 LLMConfig 默认值** — 改默认值会破坏所有依赖默认值的路径（OpenAI 兼容端点
非空 model 契约），爆炸半径远超本 change。

**不修改 CloudLLMAdapter 模型选择逻辑** — L164 行为正确，问题在调用方传值。

### Telemetry 副作用（已文档化）

Decorator 链（`CostTrackingDecorator:39` / `ComplianceDecorator:65` /
`TracingDecorator:51,57`）从 `req.params.model` 读 model 标签写入事件 / budget.
修复后 orchestrator 路径记录 `model=""`（此前是错误但非空的 `"gpt-4o-mini"`）— 属
可接受的遥测瑕疵，**不影响 token 扣费本身**（`budget_->record_llm_call` 仍按 token
数计费）。Phase D (Cost) 前需确认 cost assertion 容忍空 model.

## Scope Boundaries (In)

- ✅ 5 个潜伏站点系统性修复（每处加 `req.params.model.clear()` 或等效方案）
- ✅ 每个站点加 Oracle P1-1 风格的 **recording provider 单测**作为回归守卫
- ✅ 回归守卫在 CI (skip=1) 下确定性拦截（不依赖真实 API key）
- ✅ 修复后 Phase E/G 真实 LLM 测试可启用

## Scope Boundaries (Out)

- ❌ 不修改 `LLMConfig::model` 默认值（爆炸半径大）
- ❌ 不重构 `LLMParams = LLMConfig` 别名（per-request params 与 provider config 混淆
  是设计层 smell，独立大重构）
- ❌ 不修改 CloudLLMAdapter 模型选择逻辑（L164 行为正确）
- ❌ 不处理 token passthrough（属 `fix-yield-node-token-passthrough` 等独立 change）

## Impact

**修复站点**：
- `src/modules/executor/node_executor.cpp:356` (GenerateSubgraphNode)
- `src/modules/executor/node_executor.cpp:574` (YieldNode)
- `src/modules/skill_interpreter/skill_interpreter.cpp:657-659` (IPC llm_generate)
- `src/core/context_compactor.cpp:60` (compact 摘要)
- `src/modules/cognitive/gepa_loop.cpp:115` (GEPA 反射)

**新增测试**：5 个 recording provider 守卫（每站点 1）+ 1 个集成验证（每个站点都传空 model）

**ctest 影响**: +5 cases, baseline 224 → ~229 (无 regression)

**CI 影响**: 全部 CI 友好（不需要 API key）

**Non-goals**:
- ❌ 不实现 `/model` 运行时 provider 切换（属 `chat-model-switch-real`）
- ❌ 不解决 token 透传断裂（属 `fix-yield-node-token-passthrough` 等）
- ❌ 不修多线程 SIGSEGV（属 `fix-cloud-adapter-multithreading`，B.2 已 deferred）

## 升级触发 (Escalation)

若 `real-llm-core-coverage` Phase C-G 实施时发现 **≥3 个站点** 需同类修复（含本 change
覆盖的 5 个），本 change 应**立即升为 Phase C 前置 P0**（不再 deferred）— 因为这
5 个站点**已实测**是潜伏面，Phase C 后每发现一个就是已知面遗漏。

`real-llm-core-coverage/tasks.md` §系统性 model 遮蔽处置 已记录此触发条件。

## 验证标准

- `cmake --build build -j$(nproc)` 编译通过 (0 error, 0 warning) under debug preset
- 5 个新 recording provider 单测 PASS（每个站点）
- 1 个集成验证 PASS（5 个站点都传空 model）
- 无 API key 时全部 PASS（CI 友好）
- 全量 `ctest -j$(nproc)` baseline 224 → 229 +5 new, 0 regression
- `openspec validate fix-generation-request-model-default --strict` exit 0
- `tools/adr_lint.py` 0 errors
- `tools/docs_drift_audit.py` 0 CRITICAL drift

## 估时

| 阶段 | 内容 | 估时 |
|---|---|---|
| 1 | 5 站点代码修改（每处 1 行 + 注释解释） | 30 min |
| 2 | 5 个 recording provider 单元测试 | 1.5 h |
| 3 | 集成验证（5 站点全清空 model） | 30 min |
| 4 | 验证 + commit + archive | 15 min |
| **Total** | | **~3 h** |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| 修复后 decorator 记录 `model=""` 影响 cost/telemetry assertion | tasks.md §D 阶段 note 确认容忍；后续 telemetry 改进属 `fix-telemetry-model-label` 独立 change |
| 修复某站点后改变现有 mock 路径行为 | 每个修复点独立 test + 集成验证 + 全量 ctest |
| 5 个站点中某站点 `req` 不一定是本地变量（lambda 捕获） | grep 上下文确认每个站点 req 来源 |
