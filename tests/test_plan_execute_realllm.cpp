// tests/test_plan_execute_realllm.cpp
// 文件头注释
// 功能描述：PlanExecuteLoop 真实 deepseek LLM 端到端测试（Wave 2 / plan-execute-loop-realllm）
//          3 cases: plan_phase 真实 / verify_phase 真实 / end-to-end 真实
//          CI skip 模式 + 本地有 key 验证 (Oracle 模式 #1 + tests/AGENTS.md 模式 #1)
// 设计依据：openspec/changes/plan-execute-loop-realllm/design.md §单元测试设计
// 作者：AgenticDSL Wave 2 (post-fix-generation-request-model-default)
// 最后修改日期：2026-09-08

#include "catch_amalgamated.hpp"

#include "agenticdsl/contract/inmemory_bus.h"
#include "agenticdsl/pdk/agent_loops/loop_result.h"
#include "agenticdsl/pdk/agent_loops/plan_execute_loop.h"

#include "common/llm/llm_config.h"
#include "common/llm/llm_types.h"
#include "common/llm/llm_provider_factory.h"
#include "core/engine.h"
#include "agenticdsl/types/layered_context.h"
#include "test_helpers/real_llm_env.h"

#include <cctype>
#include <iostream>
#include <memory>
#include <string>

using namespace agenticdsl;
using hydraforge::pdk::PlanExecuteLoop;

namespace {

// 最小有效 DSL — 必须包含 /main subgraph with start/end nodes
// 与 test_plan_execute_restart.cpp 一致, 保持基线兼容
const std::string kMinimalValidDsl = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";

// 构造 DSLEngine 并注入 echo tool (Phase A A.2 模式)
std::unique_ptr<DSLEngine> make_engine_with_echo() {
  auto engine = DSLEngine::from_markdown(kMinimalValidDsl);
  engine->register_tool(
      "echo",
      ToolMetadata{"echo", "test", "test",
                   ToolCategory::ReadOnly,
                   LayerProfile::Workflow},
      [](const std::unordered_map<std::string, std::string>& args) {
        return nlohmann::json{{"echoed", args.at("message")}};
      });
  return engine;
}

// 诊断输出 helper (Phase A A.4 模式)
template <typename T>
void diag_on_failure(const T& result, const std::string& case_name) {
  if (!result.success) {
    std::cerr << "[diag:" << case_name << "] result.message: "
              << result.message << "\n"
              << "[diag:" << case_name << "] retries_used: "
              << result.retries_used << "\n"
              << "[diag:" << case_name << "] total_steps: "
              << result.total_steps << "\n"
              << "[diag:" << case_name << "] failed_phase: "
              << (result.failed_phase.has_value() ? *result.failed_phase
                                                : std::string{"<none>"})
              << "\n";
  }
}

}  // namespace

// ============================================================
// C.1: plan_phase 真实 deepseek LLM 产出可解析 DSL
// ============================================================
TEST_CASE("PlanExecuteLoop plan_phase produces parseable DSL with real LLM",
          "[plan_execute][realllm][phase-c][c1]") {
  agenticdsl::test::require_real_llm_env();
  if (agenticdsl::test::real_llm_env_skipped()) {
    SUCCEED("skipped: HYDRAFORGE_SKIP_REAL_LLM=1 (no API key or CI skip)");
    return;
  }
  // 本地有 key 时: 真实 deepseek 跑 plan_phase, 验证产出可解析 DSL
  // 注: 依赖 plan_execute_loop.h:208 的 req.params.model.clear() 已 ship
  // (Wave 2 Phase 1, fix-generation-request-model-default 体系扩展)
  auto provider = agenticdsl::test::real_llm_provider();
  auto engine = make_engine_with_echo();
  engine->set_llm_provider(std::move(provider));  // plan_phase/verify_phase 走真实 LLM

  auto bus = std::make_shared<InMemoryBus>();
  PlanExecuteLoop loop(std::move(engine), bus, 3);

  LayeredContext ctx = LayeredContext::load(nlohmann::json{
      {"system", nlohmann::json::object()},
      {"recent", nlohmann::json::object()},
      {"working", {{"data", nlohmann::json::object()}}},
      {"archive", nlohmann::json::object()},
      {"meta", nlohmann::json::object()}});

  auto result = loop.run("compute 2+3", ctx);
  diag_on_failure(result, "C.1");

  // 断言 (Oracle ship-gate Fix #2 / 模式 #3 断言分层 — 契约验证):
  REQUIRE(result.retries_used <= 3);
  REQUIRE(result.total_steps >= 1);
  // plan_phase 必须产出非空 plan (failed_phase != "Planning" 表示 plan 成功)
  if (result.failed_phase.has_value()) {
    REQUIRE(*result.failed_phase != "Planning");
  }
  // 成功路径: execute_phase 必须追加 plan → plan_appended == true
  if (result.success) {
    REQUIRE(result.final_context.working["meta"]["plan_appended"] == true);
  }
}

// ============================================================
// C.2: verify_phase 真实 deepseek LLM 含 "yes"
// ============================================================
TEST_CASE("PlanExecuteLoop verify_phase responds 'yes' with real LLM",
          "[plan_execute][realllm][phase-c][c2]") {
  agenticdsl::test::require_real_llm_env();
  if (agenticdsl::test::real_llm_env_skipped()) {
    SUCCEED("skipped: HYDRAFORGE_SKIP_REAL_LLM=1 (no API key or CI skip)");
    return;
  }
  // 本地有 key 时: 真实 deepseek 跑 verify_phase, 验证 LLM 响应含 "yes"
  // 注: 依赖 plan_execute_loop.h:254 的 req.params.model.clear() 已 ship
  auto provider = agenticdsl::test::real_llm_provider();
  auto engine = make_engine_with_echo();
  engine->set_llm_provider(std::move(provider));

  auto bus = std::make_shared<InMemoryBus>();
  PlanExecuteLoop loop(std::move(engine), bus, 3);

  LayeredContext ctx = LayeredContext::load(nlohmann::json{
      {"system", nlohmann::json::object()},
      {"recent", nlohmann::json::object()},
      {"working", {{"data", {{"x", 2}, {"y", 3}, {"sum", 5}}}}},
      {"archive", nlohmann::json::object()},
      {"meta", nlohmann::json::object()}});

  auto result = loop.run("compute 2+3", ctx);
  diag_on_failure(result, "C.2");

  // 断言 (Oracle ship-gate Fix #2 / 模式 #3 断言分层 — 能力断言):
  // verify "yes" 是契约, deepseek 在 retries (max_retries=3) 内理应回答 yes.
  // flake 即有效信号 (per AGENTS.md 模式 #3 "契约验证 → 严格 1/1").
  REQUIRE(result.retries_used <= 3);
  REQUIRE(result.total_steps >= 1);
  REQUIRE(result.success);  // verify_phase 'yes' 契约
  REQUIRE(result.message == "PlanExecuteLoop: completed successfully");
}

// ============================================================
// C.3: end-to-end 真实 deepseek run("compute 2+3") 全链路
// ============================================================
TEST_CASE("PlanExecuteLoop end-to-end run('compute 2+3') real LLM",
          "[plan_execute][realllm][phase-c][c3][e2e]") {
  agenticdsl::test::require_real_llm_env();
  if (agenticdsl::test::real_llm_env_skipped()) {
    SUCCEED("skipped: HYDRAFORGE_SKIP_REAL_LLM=1 (no API key or CI skip)");
    return;
  }
  // 本地有 key 时: 真实 deepseek 跑 plan + execute + verify 全链路
  // 多次串行 run 验证无 panic (graceful on flake, 鲁棒性)
  auto provider = agenticdsl::test::real_llm_provider();
  auto engine = make_engine_with_echo();
  engine->set_llm_provider(std::move(provider));

  auto bus = std::make_shared<InMemoryBus>();
  PlanExecuteLoop loop(std::move(engine), bus, 3);

  // 串行 2 次 run: 第一次失败时, 第二次应 graceful (不 panic)
  // 断言 (Oracle ship-gate Fix #2 / 模式 #3 断言分层 — 系统鲁棒性):
  // ≥1 次 success 反映 LLM 真实不可控 (对齐 Phase A A.4 "≥1 ok" 先例)
  int success_count = 0;
  for (int i = 0; i < 2; ++i) {
    LayeredContext ctx = LayeredContext::load(nlohmann::json{
        {"system", nlohmann::json::object()},
        {"recent", nlohmann::json::object()},
        {"working", {{"data", nlohmann::json::object()}}},
        {"archive", nlohmann::json::object()},
        {"meta", nlohmann::json::object()}});

    auto result = loop.run("compute 2+3", ctx);
    diag_on_failure(result, "C.3.iter" + std::to_string(i));

    // 核心契约: retries_used <= 3 (PlanExecuteLoop 上限)
    REQUIRE(result.retries_used <= 3);
    // 多次串行 run 不 panic (无 crash, message 有意义)
    REQUIRE_FALSE(result.message.empty());
    if (result.success) {
      success_count++;
      REQUIRE(result.message == "PlanExecuteLoop: completed successfully");
    }
  }
  REQUIRE(success_count >= 1);  // 至少 1 次成功 (LLM 不可控但链路工作)
}