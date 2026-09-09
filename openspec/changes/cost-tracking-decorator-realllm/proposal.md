## Why

`real-llm-core-coverage` Phase D 实施需求（tasks.md §Phase D），为 `CostTrackingDecorator`
建立生产级真实 LLM 计费测试覆盖。`CostTrackingDecorator::decorate_generate` 是
5 个 LLMParams 默认遮蔽潜伏面之外的**计费核心路径**（仅读 model，非构造点），
但其 budget_->record_llm_call 调用依赖准确的 token 计数 + model 标签。

**Wave 1 #1 已 ship**: `req.params.model.clear()` 在 8 站点应用（含
cost_tracking_decorator.cpp:39 读 model 路径），保证真实 LLM 下 model 标签
正确传递。

**用户原始关注**："交了 LLM 后延时会变长，需要测试来验证能工作"。
CostTrackingDecorator 的契约验证需真实 token 计数（mock 写死不可信）：
- completion_tokens 真实返回（mock 固定 5，真实 deepseek 视 prompt 而定 100-500）
- 100-token prompt 计费 ≈ prompt_tokens + completion_tokens（mock 设固定值不可信）
- 流式路径 TrackingStream 析构兜底计费（成本 < 10% 误差可接受）

**当前唯一测试** (`tests/test_cost_tracking_decorator.cpp`) 4 cases 用 MockBudget
+ MockLLMProvider 验证合约单元，**未在真实 LLM 下验证**。

## What Changes

### Scope: 真实 LLM 测试用例（3 cases）

扩展 `tests/test_cost_tracking_decorator.cpp`（既有文件）加 3 cases:

1. **D.2**: decorated generate → completion_tokens > 0 → budget 扣费 > 0
2. **D.3**: 100-token prompt 真实 LLM → 计费 ≈ prompt + completion tokens
3. **D.4**: streaming 路径下 TrackingStream 近似计费 (可接受误差 < 10%)

### 关键技术约束

**A. 复用现有 MockBudget 测试基类**:
- `tests/test_cost_tracking_decorator.cpp` 已有 `MockBudget` (extends IBudgetController,
  记录 call_count / last_tokens / last_model)
- 真实 LLM 测试也用同一 MockBudget（生产 IBudgetController 不易注入，需 wrapper）

**B. helper 复用** (同 Wave 2 plan-execute-loop-realllm 模式):
- `tests/test_helpers/real_llm_env.h` 已 ship（Phase 0 of real-llm-core-coverage）
- `agenticdsl::test::require_real_llm_env()` + `real_llm_env_skipped()` 标准 short-circuit
- `agenticdsl::test::real_llm_provider()` 构造 provider → set_llm_provider 注入

**C. 单线程 + SerializingDecorator**: 真实 deepseek 走 SerializingDecorator (Wave 1 #2
ship)，但本测试单线程（cost tracking 不并发），零 SIGSEGV 风险。

### Scope Boundaries (In)

- ✅ `tests/test_cost_tracking_decorator.cpp` 扩展 3 cases（既有文件追加）
- ✅ 每个 case 含 skip short-circuit (`real_llm_env_skipped()` early return)
- ✅ 复用既有 MockBudget 基类（真实 LLM 测试也用）
- ✅ 真实 LLM 测试断言使用 contract message + token count（pattern #3 严格 1/1）
- ✅ 复跑 `test_cost_tracking_decorator` 4 mock cases 零回归
- ✅ 跨多树 helper 维护：项目级 `real_llm_env.h` (本会话前已有)，pdk 副本保留

### Scope Boundaries (Out)

- ❌ 不修改 `cost_tracking_decorator.cpp` 生产代码（成本打点逻辑稳定契约）
- ❌ 不修改 IBudgetController 接口
- ❌ 不实施多线程并发 cost 验证（Phase G 多 worker 摘要场景属 Wave 3 Phase G scope）
- ❌ 不实现 mock provider 替换为真实 provider 的运行时切换（属 chat-model-switch）

## Impact

**修复**: 3 个真实 LLM 计费端到端 case，验证成本扣费链路真实工作
**总计 ctest 影响**: +3 cases, baseline 228 → ~231 (无 regression)
**CI 影响**: 全部 `[realllm]` tag, `HYDRAFORGE_SKIP_REAL_LLM=1` 默认 skip; 无 key 时 FAIL (硬门槛)
**风险**: mock provider 写死 token 数（completion=5），真实 LLM 返回 100-500 不等 → budget
扣费金额差异显著，需验证真实 token 计数路径

**Non-goals**:
- ❌ 不验证 budget 总额计算（cost_per_token_for × tokens），仅验证 call_count + last_tokens
- ❌ 不验证多 token 类型（prompt vs completion 分别计费属 IBudgetController 内部）

## 升级触发 (Escalation)

若 Phase D 实施时发现:
- **`completion_tokens` 真实值异常**（如 deepseek server 实际为 0 / NULL）→ 开
  `fix-cloud-llm-adapter-completion-tokens` 独立 change（生产代码）
- **cost_per_token_for 缺真实 model 名**（如 deepseek-v4-flash 未在 cost 表）→ 开
  `fix-cost-table-coverage` 独立 change
- **MockBudget 缺字段**（如需记录 cost_usd）→ 扩 MockBudget（本 change 可接受）

## 验证标准

- `cmake --build build -j$(nproc)` 编译通过 (0 error, 0 warning)
- `tests/test_cost_tracking_decorator.cpp` 3 new cases:
  - D.2: decorated generate 真实 LLM → completion_tokens > 0 → MockBudget 扣费 > 0 — PASS
  - D.3: 100-token prompt 真实 LLM → 计费 ≈ prompt + completion tokens — PASS（误差 < 20%）
  - D.4: streaming 真实 LLM → TrackingStream 析构兜底计费 < 10% 误差 — PASS
- skip 模式: 3 cases SUCCEED short-circuit (CI 友好)
- 全量 `ctest -j$(nproc)` baseline 228 → 231 +3 PASS, 0 regression
- `openspec validate cost-tracking-decorator-realllm --strict` exit 0
- `tools/adr_lint.py` 0 errors
- `tools/docs_drift_audit.py` 0 CRITICAL drift

## 估时

| 阶段 | 内容 | 估时 |
|---|---|---|
| 1 | 读 cost_tracking_decorator.cpp + 既有 test_cost_tracking_decorator.cpp | 15 min |
| 2 | 扩展 test_cost_tracking_decorator.cpp 3 cases + helper + skip | 1 h |
| 3 | CMake 验证 + ctest | 15 min |
| 4 | 真实 deepseek 验证 (本地有 key) | 30 min |
| 5 | commit + openspec validate + archive | 15 min |
| **Total** | | **~2.5 h** |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| completion_tokens 真实值波动大（mock 设 5 vs 真实 100-500）→ 断言不稳定 | 断言 `last_tokens > 0` + `last_tokens <= 2000` (经验上限) + 不设具体值 |
| 流式析构兜底计费 max_tokens_estimate 远大于实际 → cost 误差 > 10% | 断言 `last_tokens > 0` + `last_tokens <= max_tokens_estimate` (兜底合理性) |
| MockBudget 不记录 cost_usd → 无法验证成本金额 | 记录 call_count + last_tokens 已够验证合约；cost_usd 验算属 IBudgetController 单测范围 |
| model 遮蔽未修前实施 → 真实 LLM 必撞墙 | Wave 1 #1 已 ship 8 站点修复，含 cost_tracking_decorator 读 model 路径 |