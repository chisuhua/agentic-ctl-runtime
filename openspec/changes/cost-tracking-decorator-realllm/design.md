# Design — cost-tracking-decorator-realllm

## 测试架构

### 测试目标

在 `tests/test_cost_tracking_decorator.cpp`（既有文件）追加 3 cases，验证
`CostTrackingDecorator` 在真实 deepseek LLM 下的端到端计费契约：

| Case | 验证路径 | 期望 |
|---|---|---|
| D.2 | `decorate_generate` 真实 LLM | completion_tokens > 0, MockBudget 扣费 > 0 |
| D.3 | 100-token prompt 真实 LLM | MockBudget.last_tokens ≈ prompt + completion tokens (误差 < 20%) |
| D.4 | `decorate_generate_stream` 真实 LLM | TrackingStream 析构兜底计费 (max_tokens_estimate 兜底, 误差 < 10%) |

### 既有 test_cost_tracking_decorator.cpp 结构

- `MockBudget` 类（extends IBudgetController）：记录 call_count / last_tokens / last_model
- 4 个 TEST_CASE（mock provider 验证合约单元）：
  1. "CostTrackingDecorator charges on success" — sync 计费
  2. "CostTrackingDecorator no charge on error" — error 路径不计费
  3. "CostTrackingDecorator charges after stream end" — stream next() nullopt 时计费
  4. "CostTrackingDecorator stream destructor fallback" — 析构兜底

### 复用既有 MockBudget

3 个新真实 LLM 测试**复用同一 MockBudget 类**（namespace 内已有），不重写：
- mock provider 4 cases 用同一 MockBudget（已有）
- 真实 LLM 3 cases 用同一 MockBudget（新增）

### CostTrackingDecorator 接口关键点

```cpp
// decorate_generate
Result<GenerationResult, LLMError> decorate_generate(
    const GenerationRequest& req,
    Result<GenerationResult, LLMError> inner_result) override {
  if (inner_result.has_value()) {
    int total_tokens = inner_result.value().prompt_tokens +
                       inner_result.value().completion_tokens;
    // ⚠️ Wave 1 #1 已 ship: req.params.model.clear() 保证 adapter fallback
    //    (cost_tracking_decorator.cpp:39 读 model 路径仍需 model 字段非空
    //     才能查 cost_per_token_for; 但 Phase 1 简化 path 用 model_name_ 构造参数,
    //     此处仅用于 audit/telemetry)
    budget_->record_llm_call(total_tokens, req.params.model);
  }
  return inner_result;
}

// decorate_generate_stream: TrackingStream 包装 inner, 流结束 next() 返回 nullopt
// 时或析构时调 budget_->record_llm_call(max_tokens_estimate_, model_name_)
```

### 单元测试设计 (3 cases 新增)

#### D.2 — decorated generate → completion_tokens > 0 → budget 扣费 > 0

```cpp
TEST_CASE("CostTrackingDecorator real LLM charge success",
          "[cost_tracking][realllm][phase-d][d2]") {
  agenticdsl::test::require_real_llm_env();
  if (agenticdsl::test::real_llm_env_skipped()) {
    SUCCEED("skipped"); return;
  }

  auto provider = agenticdsl::test::real_llm_provider();
  auto budget = std::make_shared<MockBudget>();
  CostTrackingDecorator decorator(std::move(provider), budget, "deepseek-v4-flash", 500);

  GenerationRequest req;
  req.prompt = "Say OK";
  req.params.model.clear();  // Wave 1 #1 pattern: 让 CloudLLMAdapter fallback

  auto result = decorator.generate(req, {});
  REQUIRE(result.has_value());

  // 核心契约: 真实 LLM completion_tokens > 0 → budget 扣费 > 0
  REQUIRE(budget->call_count.load() == 1);
  REQUIRE(budget->last_tokens.load() > 0);
  REQUIRE(budget->last_tokens.load() <= 2000);  // 经验上限 (防 100-token → 100k token bug)
}
```

#### D.3 — 100-token prompt 真实 LLM → 计费 ≈ prompt + completion tokens

```cpp
TEST_CASE("CostTrackingDecorator 100-token prompt real LLM charge",
          "[cost_tracking][realllm][phase-d][d3]") {
  agenticdsl::test::require_real_llm_env();
  if (agenticdsl::test::real_llm_env_skipped()) { SUCCEED("skipped"); return; }

  auto provider = agenticdsl::test::real_llm_provider();
  auto budget = std::make_shared<MockBudget>();
  CostTrackingDecorator decorator(std::move(provider), budget, "deepseek-v4-flash", 500);

  GenerationRequest req;
  req.prompt = std::string(100, 'x');  // 100-char prompt (≈ 25 tokens)
  req.params.model.clear();

  auto result = decorator.generate(req, {});
  REQUIRE(result.has_value());

  // 核心契约: 计费 ≈ prompt_tokens + completion_tokens
  int expected = result.value().prompt_tokens + result.value().completion_tokens;
  int actual = budget->last_tokens.load();
  REQUIRE(budget->call_count.load() == 1);
  REQUIRE(actual == expected);  // exact match (decorator pass-through)
  REQUIRE(actual > 0);  // sanity: 真实 LLM 必有 token
}
```

#### D.4 — streaming 路径 TrackingStream 析构兜底计费

```cpp
TEST_CASE("CostTrackingDecorator streaming real LLM destructor fallback",
          "[cost_tracking][realllm][phase-d][d4]") {
  agenticdsl::test::require_real_llm_env();
  if (agenticdsl::test::real_llm_env_skipped()) { SUCCEED("skipped"); return; }

  auto provider = agenticdsl::test::real_llm_provider();
  auto budget = std::make_shared<MockBudget>();
  CostTrackingDecorator decorator(std::move(provider), budget, "deepseek-v4-flash", 500);

  GenerationRequest req;
  req.prompt = "Stream 3 tokens";
  req.params.model.clear();

  // 模拟调用方提前析构: unique_ptr scope
  {
    auto stream = decorator.generate_stream(req, {});
    REQUIRE(stream != nullptr);
    // 不 poll next() — 立即析构, 触发 TrackingStream::~TrackingStream 兜底计费
  }

  // 核心契约: 流未消费但析构兜底
  REQUIRE(budget->call_count.load() == 1);
  int fallback_tokens = budget->last_tokens.load();
  REQUIRE(fallback_tokens > 0);
  REQUIRE(fallback_tokens <= 500);  // <= max_tokens_estimate_ (兜底合理性)
}
```

### CI / 验证流程

1. **CI 默认 skip** (`HYDRAFORGE_SKIP_REAL_LLM=1`): 3 cases SUCCEED early return
2. **本地有 key**: 真实 LLM 跑 — D.2/D.3/D.4 验证
3. **无 key 无 skip**: helper FAIL 硬门槛

### 失败诊断

每 case 含:
```cpp
if (!result.has_value()) {
  std::cerr << "[diag:D.2] result.error.code=" << static_cast<int>(result.error().code)
            << " message=" << result.error().message << "\n";
}
```

## 实施顺序

1. **Phase 1 (测试代码)**: 1 commit
   - `tests/test_cost_tracking_decorator.cpp` 追加 3 cases + 复用 MockBudget
   - CMake GLOB 自动注册（零 CMake 变更）
2. **Phase 2 (验证)**:
   - skip 模式 ctest: 3 cases SUCCEED + 4 mock cases 零回归, baseline 228 → 231
   - 真实 deepseek: 本地有 key 跑
   - 失败 case 记录给 Phase D 后续 follow-up
3. **Phase 3 (commit + archive)**: 1 commit + archive

每步骤独立 commit (atomic commit 原则).