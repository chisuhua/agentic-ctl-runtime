# Spec: cost-tracking-decorator-realllm

## Purpose

为 `CostTrackingDecorator` 建立生产级真实 LLM 计费测试覆盖。验证真实 deepseek
下 cost tracking 链路工作 — completion_tokens 真实返回、MockBudget 扣费触发、
流式路径 TrackingStream 析构兜底计费。

## ADDED Requirements

### Requirement: decorated generate 真实 LLM SHALL charge budget on success

`CostTrackingDecorator::decorate_generate` SHALL 在真实 deepseek LLM 成功返回时触发 `budget_->record_llm_call(total_tokens, req.params.model)` 调用 (call_count=1, last_tokens > 0), 验证 cost 扣费链路真实工作.

#### Scenario: CostTrackingDecorator real LLM charge success
- GIVEN real deepseek configured
- AND CostTrackingDecorator 构造 (provider + MockBudget + "deepseek-v4-flash" + max_tokens_estimate=500)
- WHEN `decorator.generate(req, {})` 返回 success (含 prompt_tokens + completion_tokens)
- THEN `MockBudget.call_count == 1`
- AND `MockBudget.last_tokens > 0`
- AND `MockBudget.last_tokens <= 2000` (经验上限, 防 100-token → 100k token bug)
- AND `MockBudget.last_model` 透传自 req.params.model

### Requirement: 100-token prompt 真实 LLM 计费 SHALL equal prompt + completion tokens

`CostTrackingDecorator::decorate_generate` SHALL 在真实 deepseek LLM 成功返回时传递 `prompt_tokens + completion_tokens` 之和至 `MockBudget.last_tokens` (exact match, 非近似), 验证 token 计数路径无 off-by-one / 重计 / 漏计 bug.

#### Scenario: 100-token prompt real LLM charge exact match
- GIVEN real deepseek configured
- AND prompt 为 100 char (≈ 25 token)
- WHEN `decorator.generate(req, {})` 返回 success
- THEN `MockBudget.call_count == 1`
- AND `MockBudget.last_tokens == result.value().prompt_tokens + result.value().completion_tokens` (exact match)
- AND `MockBudget.last_tokens > 0` (sanity: 真实 LLM 必有 token)

### Requirement: streaming 路径 TrackingStream 析构兜底 SHALL charge budget

`CostTrackingDecorator::decorate_generate_stream` 返回的 `TrackingStream` SHALL 在析构时 (无论 next() 是否 poll 过 nullopt) 触发 `budget_->record_llm_call(max_tokens_estimate_, model_name_)`, 兜底保证 budget 无遗漏.

#### Scenario: CostTrackingDecorator streaming real LLM destructor fallback
- GIVEN real deepseek configured
- AND 模拟调用方提前析构 (unique_ptr scope 内不 poll next)
- WHEN `stream` 析构触发 `TrackingStream::~TrackingStream`
- THEN `MockBudget.call_count == 1`
- AND `MockBudget.last_tokens > 0`
- AND `MockBudget.last_tokens <= max_tokens_estimate_` (兜底合理性)

### Requirement: scope 边界 (Out of Scope)

本 change SHALL NOT 修改 `cost_tracking_decorator.cpp` 生产代码或 `IBudgetController` 接口; 下列 SHALL 明确排除在 scope 外.

#### Scenario: cost_tracking_decorator.cpp 生产代码 SHALL NOT be changed
- 理由: cost 打点逻辑稳定契约 (REQ-IPD-002 Sprint 5 ship), 不修改
- AND max_tokens_estimate_ 兜底机制已 ship, 不调整
- AND prompt vs completion 分别计费属 IBudgetController 内部, 不在本 change

#### Scenario: Multi-thread CostTrackingDecorator SHALL NOT be in this change
- 理由: Phase D 单线程, CostTrackingDecorator::decorate_generate 自身无并发
- AND 多 worker 并发 cost 验证属 Wave 3 Phase G (ContextCompactor) scope

#### Scenario: IBudgetController 接口扩展 SHALL NOT be in this change
- 理由: MockBudget 已记录必要字段 (call_count / last_tokens / last_model)
- AND IBudgetController 接口稳定 (Sprint 5 ship), 不扩字段

## 验证标准

- `cmake --build build -j$(nproc)` 编译通过 (0 error, 0 warning)
- `tests/test_cost_tracking_decorator.cpp` 3 new cases:
  - D.2: decorated generate 真实 LLM → completion_tokens > 0 → budget 扣费 > 0 — PASS
  - D.3: 100-token prompt 真实 LLM → 计费 == prompt + completion tokens — PASS
  - D.4: streaming 真实 LLM → TrackingStream 析构兜底计费 — PASS
- skip 模式: 3 cases SUCCEED short-circuit (CI 友好)
- 既有 4 mock cases 零回归 (charges on success / no charge on error / charges after stream end /
  stream destructor fallback)
- 全量 `ctest -j$(nproc)` baseline 228 → 231 +3 PASS, 0 regression
- `openspec validate cost-tracking-decorator-realllm --strict` exit 0
- `tools/adr_lint.py` 0 errors
- `tools/docs_drift_audit.py` 0 CRITICAL drift

## 依赖

| 上游 | 状态 | 影响 |
|---|---|---|
| `real-llm-core-coverage` Phase 0+A | ✅ SHIPPED | helper 自测已可用 (real_llm_env.h) |
| Wave 1 #1 fix-generation-request-model-default | ✅ SHIPPED | req.params.model.clear() 已 ship 8 站点, 含 cost_tracking_decorator 读 model 路径 |
| Wave 1 #2 fix-cloud-adapter-multithreading | ✅ SHIPPED | 真实 deepseek 走 SerializingDecorator (本测试单线程, 零 SIGSEGV 风险) |

| 下游 | 内容 |
|---|---|
| `real-llm-core-coverage` Phase E/G 真实 LLM | cost 验证就绪, 不需重测 |
| pdk_chat_demo 真实 LLM agent 循环 | 计费链路端到端工作, 可用真实 deepseek |

## References

- **设计依据**: `src/common/llm/cost_tracking_decorator.cpp:32` (decorate_generate) +
  `:46` (decorate_generate_stream) + `:67` (TrackingStream 析构兜底)
- **既有测试**: `tests/test_cost_tracking_decorator.cpp` (4 mock cases, MockBudget 基类)
- **关联修复**: Wave 1 #1 `fix-generation-request-model-default` (req.params.model.clear())
- **测试模式**: Wave 2 `plan-execute-loop-realllm` (require_real_llm_env + skip short-circuit)
- **追溯 ADR**: REQ-IPD-002 (Phase 5 Budget Hole 修复, Sprint 5 ship)