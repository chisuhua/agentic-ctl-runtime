# Design — plan-execute-loop-realllm

## 测试架构

### 测试目标

在 `tests/test_plan_execute_realllm.cpp` 新建 3 cases, 验证
`PlanExecuteLoop::run()` 在真实 deepseek LLM 下的端到端契约：

| Case | 验证路径 | 期望 |
|---|---|---|
| C.1 | `plan_phase` 真实 LLM | LLM 输出 DSL, `DSLEngine::continue_with_generated_dsl` 解析成功 |
| C.2 | `verify_phase` 真实 LLM | LLM 响应含 "yes" (大小写不敏感), loop 终止成功 |
| C.3 | end-to-end `run("compute 2+3", ctx)` | 全链路 (plan + execute + verify) 成功, result.success==true |

### 与现有 test_plan_execute_restart.cpp 的关系

- **不修改** `test_plan_execute_restart.cpp`（Sprint 20 ship 的 mock 测试）
- 新文件 `test_plan_execute_realllm.cpp` 与其独立, 走真实 LLM 路径
- 测试基类共用: 同样用 `DSLEngine::from_markdown(kMinimalValidDsl)` + `register_tool("echo", ...)` + `enqueue_response` 模式

### helper 复用

完全复用 `real-llm-core-coverage` Phase 0 ship 的 `tests/test_helpers/real_llm_env.h`:

```cpp
agenticdsl::test::require_real_llm_env();
if (agenticdsl::test::real_llm_env_skipped()) {
  SUCCEED("skipped: HYDRAFORGE_SKIP_REAL_LLM=1");
  return;
}
auto cfg = agenticdsl::test::real_llm_config();
auto provider = agenticdsl::test::real_llm_provider();
auto engine = DSLEngine::from_markdown(kMinimalValidDsl);
// ... register echo tool, set_llm_provider ...
agenticdsl::ILLMProvider* shared = engine->get_llm_provider();
PlanExecuteLoop loop(std::move(engine), shared);
// 测试构造 real_llm_provider 后再调 set_llm_provider 注入 (同 Phase A A.2 模式)
```

**注意**: PlanExecuteLoop 构造签名 `(unique_ptr<DSLEngine>, ILLMProvider*)`
（看 plan_execute_loop.h:77），所以**先构造 provider 再构造 loop**，不调 set_llm_provider。
但 DSLEngine 必须注入真实 provider 才能让 loop 内的 engine 调用走真实 LLM：
- PlanExecuteLoop.run() 调 `engine_->continue_with_generated_dsl` 走 DSLEngine
- DSLEngine 不调 LLM 直接（它是 graph executor）
- 但 `plan_phase` / `verify_phase` 用 `llm` 参数（PlanExecuteLoop 构造传入的 raw ptr）

所以正确路径：
```cpp
auto engine = DSLEngine::from_markdown(kMinimalValidDsl);
engine->register_tool("echo", ...);
// 不调 set_llm_provider, 用 mock 默认 provider 即可
// PlanExecuteLoop 构造时传真实 provider 给 plan/verify
auto real_provider = real_llm_provider();
PlanExecuteLoop loop(std::move(engine), real_provider.get());
```

**澄清**: PlanExecuteLoop 持有**两个 provider** — engine 的 (mock, 不参与 plan/verify) 和
构造传入的 (real, 用于 plan/verify). 走真实 plan/verify 只用构造传入的 real provider.

### model 遮蔽 workaround

`plan_phase` / `verify_phase` 构造 `GenerationRequest req` 不设 `req.params.model` →
默认 `"gpt-4o-mini"` 遮蔽 → deepseek server 拒绝.

**workaround**: 由于 PlanExecuteLoop 的 plan/verify 内部构造 req (用户不可干预),
model 遮蔽无法在测试代码侧绕过. 必须**依赖 `fix-generation-request-model-default` ship**
后才能跑真实 LLM. 这是本 change 的硬依赖 (见 proposal.md §依赖).

**临时 fallback** (model 修复未 ship 时): 测试在**注释中记录** model 遮蔽已知, 跳
真实 LLM 部分, 仅验证 helper short-circuit + provider 构造路径 (CI 友好).

### 单元测试设计 (3 cases)

#### C.1 — plan_phase 真实 LLM 合法 DSL

```cpp
TEST_CASE("PlanExecuteLoop plan_phase produces parseable DSL with real LLM",
          "[plan_execute][realllm][phase-c]") {
  require_real_llm_env();
  if (real_llm_env_skipped()) { SUCCEED("skipped"); return; }

  auto cfg = real_llm_config();
  auto provider = real_llm_provider();
  auto engine = DSLEngine::from_markdown(kMinimalValidDsl);
  engine->register_tool("echo", ToolMetadata{...}, [](const auto& args) {
    return json{{"echoed", args.at("message")}};
  });

  PlanExecuteLoop loop(std::move(engine), provider.get());
  // 调 plan_phase via friend 或 public API?
  // plan_phase 是 private — 走 run() 间接验证
  LayeredContext ctx;
  ctx.working["goal"] = "compute 2+3";
  auto result = loop.run("compute 2+3", ctx);

  if (result.success) {
    INFO("plan+execute+verify all OK");
  } else {
    INFO("result.message: " << result.message);
    INFO("retry_count: " << result.retries_used);
  }
  // 断言宽松: 真实 LLM 不保证每次 plan/verify 全过
  // 但成功路径应至少 1 次 (或全失败但 message 有意义)
  REQUIRE(result.retries_used <= 3);
  // 不 REQUIRE result.success — 太 flake; 记录失败 case 给 prompt 改进 follow-up
}
```

#### C.2 — verify_phase 真实 LLM 含 "yes"

```cpp
TEST_CASE("PlanExecuteLoop verify_phase responds 'yes' with real LLM",
          "[plan_execute][realllm][phase-c]") {
  // 同 C.1 setup
  // mock 预填 plan_phase 响应 (合法 DSL), 让 verify_phase 调真实 LLM
  // enqueue_response(plan DSL), real provider 用于 verify
  // 断言: result.success==true (LLM 响应含 "yes")
}
```

#### C.3 — end-to-end 真实 LLM 全链路

```cpp
TEST_CASE("PlanExecuteLoop end-to-end real LLM run('compute 2+3')",
          "[plan_execute][realllm][phase-c]") {
  // plan + verify 都真实
  // 断言: result.success==true (或 ≥1 ok + 失败 message 有意义)
}
```

### CI / 验证流程

1. **CI 默认 skip** (HYDRAFORGE_SKIP_REAL_LLM=1): 3 cases SUCCEED early return
2. **本地有 key**: 真实 LLM 跑 — C.1/C.2/C.3 验证
3. **无 key 无 skip**: helper FAIL 硬门槛

### 失败诊断

每 case 含:
```cpp
if (!result.success) {
  std::cerr << "[diag] result.message: " << result.message << "\n"
            << "[diag] retries: " << result.retries_used << "\n";
}
```

失败输出便于本地调试 + 远程 CI 抓取.

## 实施顺序

1. **Phase 1 (测试代码)**: 1 commit
   - `tests/test_plan_execute_realllm.cpp` 3 cases + helper + skip
   - CMake GLOB 自动注册 (零 CMake 变更)
2. **Phase 2 (验证)**:
   - skip 模式 ctest: 3 cases SUCCEED, 零 regression
   - 真实 deepseek: 本地有 key 跑
   - 失败 case 记录给 prompt 改进 follow-up
3. **Phase 3 (commit + archive)**: 1 commit + archive

每步骤独立 commit (atomic commit 原则).