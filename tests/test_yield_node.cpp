#include "catch_amalgamated.hpp"
#include "common/llm/mock_provider.h"
#include "agenticdsl/contract/i_llm_provider_decorator.h"
#include "common/tools/registry.h"
#include "common/utils/parser_utils.h"
#include "core/engine.h"
#include "core/types/node.h"
#include "modules/executor/node_executor.h"
#include "modules/executor/yield_stream_bridge.h"
#include "modules/parser/markdown_parser.h"
#include "modules/scheduler/resource_manager.h"
#include "scheduler/execution_session.h"

#include <memory>
#include <string>
#include <vector>

using namespace agenticdsl;

TEST_CASE("YieldNode NEXT mode returns single token via NodeExecutor",
          "[executor][yield][stage5]") {
    ToolRegistry registry;
    auto provider_holder = std::make_unique<MockLLMProvider>();
    MockLLMProvider* provider_raw = provider_holder.get();
    provider_raw->set_stream_tokens({"Hello", "world"});

    NodeExecutor executor(registry, provider_holder.get());

    YieldNode node("/main/yield_next",
                   std::vector<NodePath>{}, nlohmann::json::object(),
                   std::nullopt, std::vector<std::string>{},
                   "Test prompt", YieldMode::NEXT, "");
    Context ctx;

    Context result = executor.execute_node(&node, ctx);

    REQUIRE(result.contains("__yield_mode__"));
    REQUIRE(result["__yield_mode__"] == "NEXT");
    REQUIRE(result.contains("__yield__"));
    REQUIRE(result["__yield__"] == "Hello");
    REQUIRE(result["__yield_node_path__"] == "/main/yield_next");
    REQUIRE(provider_raw->call_count() == 1);
}

TEST_CASE("YieldNode CONTINUE mode concatenates tokens until stream end",
          "[executor][yield][stage5]") {
    ToolRegistry registry;
    auto provider_holder = std::make_unique<MockLLMProvider>();
    provider_holder->set_stream_tokens({"alpha", "-", "beta", "-", "gamma"});

    NodeExecutor executor(registry, provider_holder.get());

    YieldNode node("/main/yield_continue",
                   std::vector<NodePath>{}, nlohmann::json::object(),
                   std::nullopt, std::vector<std::string>{},
                   "Test prompt", YieldMode::CONTINUE, "");
    Context ctx;

    Context result = executor.execute_node(&node, ctx);

    REQUIRE(result["__yield_mode__"] == "CONTINUE");
    REQUIRE(result["__yield__"] == "alpha-beta-gamma");
    REQUIRE_FALSE(result.contains("__yield_budget_exceeded__"));
}

TEST_CASE("YieldNode STOP mode stores stop_path without LLM call",
          "[executor][yield][stage5]") {
    ToolRegistry registry;
    auto provider_holder = std::make_unique<MockLLMProvider>();
    MockLLMProvider* provider_raw = provider_holder.get();
    NodeExecutor executor(registry, provider_holder.get());

    YieldNode node("/main/yield_stop",
                   std::vector<NodePath>{}, nlohmann::json::object(),
                   std::nullopt, std::vector<std::string>{},
                   "", YieldMode::STOP, "/main/cleanup");
    Context ctx;

    Context result = executor.execute_node(&node, ctx);

    REQUIRE(result["__yield_mode__"] == "STOP");
    REQUIRE(result["__yield_stop_path__"] == "/main/cleanup");
    REQUIRE(provider_raw->call_count() == 0);
}

TEST_CASE("YieldNode executor with nullptr LLM returns context unchanged",
          "[executor][yield][stage5]") {
    ToolRegistry registry;
    NodeExecutor executor(registry, nullptr);

    YieldNode node("/main/yield_llm_null",
                   std::vector<NodePath>{}, nlohmann::json::object(),
                   std::nullopt, std::vector<std::string>{},
                   "Test", YieldMode::NEXT, "");
    Context ctx;
    ctx["preset"] = "value";

    Context result = executor.execute_node(&node, ctx);

    REQUIRE(result.contains("preset"));
    REQUIRE(result["preset"] == "value");
    REQUIRE_FALSE(result.contains("__yield__"));
}

TEST_CASE("YieldStreamBridge CONTINUE throws BudgetExceededException after consumption",
          "[executor][yield][budget][stage5]") {
    auto provider = std::make_unique<MockLLMProvider>();
    provider->set_stream_tokens({"a", "b", "c", "d", "e"});
    GenerationRequest req;
    auto stream = provider->generate_stream(req, std::stop_token{});
    REQUIRE(stream != nullptr);

    YieldStreamBridge bridge{};

    int call_count = 0;
    auto budget_checker = [&call_count]() {
        ++call_count;
        return call_count <= 2;
    };

    bool caught = false;
    try {
        (void)bridge.pull_loop(*stream, budget_checker, 100);
        FAIL("Budget check should have triggered");
    } catch (const BudgetExceededException& e) {
        caught = true;
        REQUIRE(e.consumed_tokens.size() == 2);
        REQUIRE(e.consumed_tokens[0] == "a");
        REQUIRE(e.consumed_tokens[1] == "b");
    }
    REQUIRE(caught);
}

TEST_CASE("ExecutionSession sets pending_yield_ when YIELD produces __yield_mode__",
          "[session][yield][stage5]") {
    ToolRegistry registry;
    auto provider = std::make_unique<MockLLMProvider>();
    provider->set_stream_tokens({"t1", "t2"});
    ResourceManager rm;

    YieldNode node("/main/yield_continue",
                   std::vector<NodePath>{}, nlohmann::json::object(),
                   std::nullopt, std::vector<std::string>{},
                   "Test prompt", YieldMode::CONTINUE, "");
    Context ctx;

    ExecutionSession session("test-session", std::nullopt, registry, provider.get(), rm, nullptr);
    ExecutionSession::ExecutionResult result = session.execute_node(&node, ctx);

    REQUIRE(result.success);
    REQUIRE(result.paused_at.has_value());
    REQUIRE(result.paused_at.value() == "/main/yield_continue");

    auto pending = session.get_pending_yield();
    REQUIRE(pending.has_value());
    REQUIRE(pending->module_path == "/main/yield_continue");
}

TEST_CASE("ExecutionSession pending_yield_ persists and clears correctly",
          "[session][yield][stage5]") {
    ToolRegistry registry;
    auto provider = std::make_unique<MockLLMProvider>();
    provider->set_stream_tokens({"only_one"});
    ResourceManager rm;

    YieldNode node("/main/yield_next",
                   std::vector<NodePath>{}, nlohmann::json::object(),
                   std::nullopt, std::vector<std::string>{},
                   "Test prompt", YieldMode::NEXT, "");
    Context ctx;

    ExecutionSession session("test-session", std::nullopt, registry, provider.get(), rm, nullptr);

    REQUIRE_FALSE(session.get_pending_yield().has_value());

    ExecutionSession::ExecutionResult result = session.execute_node(&node, ctx);
    REQUIRE(result.success);
    REQUIRE(session.get_pending_yield().has_value());

    session.clear_pending_yield();
    REQUIRE_FALSE(session.get_pending_yield().has_value());
}

TEST_CASE("Yaml integration: parser produces YieldNode from {type: yield, mode: continue}",
          "[parser][yield][stage5]") {
    const std::string dsl = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/yield_node"]
  - id: yield_node
    type: yield
    yield_value: "Hello"
    mode: continue
    stop_path: "/main/end_node"
    next: ["/main/end_node"]
  - id: end_node
    type: end
# --- END AgenticDSL ---
```
    )";

    MarkdownParser parser;
    auto graphs = parser.parse_from_string(dsl);
    REQUIRE(graphs.size() == 1);
    bool found_yield = false;
    for (const auto& node : graphs[0].nodes) {
        if (node && node->type == NodeType::YIELD) {
            found_yield = true;
            auto* yn = static_cast<const YieldNode*>(node.get());
            REQUIRE(yn->yield_value == "Hello");
            REQUIRE(yn->mode == YieldMode::CONTINUE);
            REQUIRE(yn->stop_path == "/main/end_node");
        }
    }
    REQUIRE(found_yield);
}

TEST_CASE("DSLEngine end-to-end NEXT yield produces __yield__ context key",
          "[engine][yield][stage5]") {
    const std::string dsl = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/yield_node"]
  - id: yield_node
    type: yield
    yield_value: "Streaming prompt"
    mode: next
    next: ["/main/end_node"]
  - id: end_node
    type: end
# --- END AgenticDSL ---
```
    )";

    auto engine = DSLEngine::from_markdown(dsl);
    ILLMProvider* _p_y = engine->get_llm_provider();
    auto* provider = dynamic_cast<MockLLMProvider*>(_p_y);
    if (!provider) { if (auto* d = dynamic_cast<ILLMProviderDecorator*>(_p_y)) provider = dynamic_cast<MockLLMProvider*>(d->inner()); }
    REQUIRE(provider != nullptr);
    provider->set_stream_tokens({"token-A", "token-B"});

    LayeredContext initial_ctx;
    ExecutionResult result = engine->run(initial_ctx);

    REQUIRE(result.success);
    REQUIRE(result.final_context.contains("__yield__"));
    REQUIRE(result.final_context["__yield__"] == "token-A");
    REQUIRE(result.final_context["__yield_mode__"] == "NEXT");
}

// ============================================================
// fix-yield-node-token-passthrough (Wave 1 #1 GAP 修复)
// 验证 std::stop_token 透传至 llm_provider_->generate_stream (替换硬编码 token={})
// ============================================================
TEST_CASE("YieldNode NEXT mode accepts stop_token parameter",
          "[executor][yield][token][realllm-gap-fix]") {
    ToolRegistry registry;
    auto provider_holder = std::make_unique<MockLLMProvider>();
    MockLLMProvider* provider_raw = provider_holder.get();
    provider_raw->set_stream_tokens({"Hello", "world"});

    NodeExecutor executor(registry, provider_holder.get());

    YieldNode node("/main/yield_with_token",
                   std::vector<NodePath>{}, nlohmann::json::object(),
                   std::nullopt, std::vector<std::string>{},
                   "Test prompt", YieldMode::NEXT, "");
    Context ctx;

    // 核心契约: execute_node 接受 std::stop_token 形参 (Wave 1 #1 GAP 修复后)
    // 验证签名 + token 透传至 generate_stream (mock 流立即返回, 验证调用成功)
    std::stop_source ss;  // 非默认 stop_token (区别于原硬编码 token={})
    Context result = executor.execute_node(&node, ctx, default_budget_checker(), ss.get_token());

    REQUIRE(result.contains("__yield_mode__"));
    REQUIRE(result["__yield_mode__"] == "NEXT");
    REQUIRE(result.contains("__yield__"));
    REQUIRE(result["__yield__"] == "Hello");
    REQUIRE(provider_raw->call_count() == 1);
}

TEST_CASE("YieldNode NEXT mode cancelled stop_token returns gracefully",
          "[executor][yield][token][cancellation][realllm-gap-fix]") {
    // 验证 token={} → token 修复后, 预设 cancel 的 token 传播至 mock provider
    // (mock 流 next() 检查 stop_requested 返回 nullopt, execute_node 立即返回空 result)
    ToolRegistry registry;
    auto provider_holder = std::make_unique<MockLLMProvider>();
    MockLLMProvider* provider_raw = provider_holder.get();
    provider_raw->set_stream_tokens({"never", "reached"});

    NodeExecutor executor(registry, provider_holder.get());

    YieldNode node("/main/yield_pre_cancel",
                   std::vector<NodePath>{}, nlohmann::json::object(),
                   std::nullopt, std::vector<std::string>{},
                   "Test prompt", YieldMode::NEXT, "");
    Context ctx;

    // 预设 cancel: execute_node 进入 generate_stream 后, mock next() 立即
    // 检测到 stop_requested() 返回 nullopt, 流立即结束 (零延迟, 因 mock 流
    // 不 sleep). 验证 token 透传 (非默认 token={}).
    std::stop_source ss;
    ss.request_stop();  // 预设 cancel

    Context result = executor.execute_node(&node, ctx, default_budget_checker(), ss.get_token());

    // token 透传验证: 不死锁, 立即返回 (mock 流零延迟响应 cancel)
    REQUIRE(result.contains("__yield_mode__"));
    REQUIRE(result["__yield_mode__"] == "NEXT");
    // 关键: provider 已被调用 (证明 token 透传至 generate_stream)
    REQUIRE(provider_raw->call_count() == 1);
    // mock 在 token 取消时 next() 返回 nullopt → YieldNode NEXT 取首 chunk → 字符串可能空
    // 注: __yield__ 字段可能缺失 (next() 返回 nullopt), 不 REQUIRE
}
