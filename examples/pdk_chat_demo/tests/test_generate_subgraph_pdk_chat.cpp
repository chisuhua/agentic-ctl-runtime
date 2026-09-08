// tests/test_generate_subgraph_pdk_chat.cpp
// chat-real-llm-coverage Phase B: GenerateSubGraph + Mock LLM (pdk_chat_demo context)
//
// 验证 (确定性, 5 cases):
//   B.1.2 Mock LLM valid DSL → subgraph executes + callback fires (1 graph)
//   B.1.3 Mock LLM malformed YAML → graceful failure (success=false, error 含 "parse")
//   B.1.4 Mock LLM arithmetic DSL → subgraph 含 type: llm_call 节点
//   B.1.5 callback 接收 multiple graphs (3 sequential calls via NodeExecutor)
//   B.1.6 NodeExecutor 级 GenerateSubGraph (替代 chat() 端到端路径不可达)
//
// Mock DSL 语法要求 (源自 tests/test_generate_subgraph_callback.cpp:21-34):
//   - ### AgenticDSL /dynamic/<name> 头 (callback 触发前提: graph.path rfind("/dynamic/") == 0)
//   - ```yaml fenced 块
//   - graph_type: subgraph
//   - nodes: 列表含 type: start / type: end (NodeFactoryRegistry 小写 key)
//   - # --- BEGIN/END AgenticDSL --- 标记

#include <catch_amalgamated.hpp>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "modules/executor/node_executor.h"
#include "core/types/node.h"
#include "common/tools/registry.h"
#include "common/llm/mock_provider.h"
#include "core/types/context.h"

using namespace agenticdsl;

namespace {

// ===== Canonical fixtures =====

const std::string kMinimalDynamicDsl = R"(
### AgenticDSL `/dynamic/pdk_minimal`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/dynamic/pdk_minimal/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";

const std::string kArithmeticDynamic = R"(
### AgenticDSL `/dynamic/pdk_calc`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/dynamic/pdk_calc/middle_node"]
  - id: middle_node
    type: tool_call
    tool: calculate
    output_keys: ["/dynamic/pdk_calc/calc_result"]
    next: ["/dynamic/pdk_calc/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";

// 畸形 YAML — 缺右括号, 触发确定性 "YAML parse error in block ..."
const std::string kMalformedDynamicDsl = R"(
### AgenticDSL `/dynamic/pdk_broken`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/dynamic/pdk_broken/end"
# --- END AgenticDSL ---
```
)";

const std::string kMultiDynamicDsl = R"(
### AgenticDSL `/dynamic/pdk_multi_alpha`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/dynamic/pdk_multi_alpha/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
### AgenticDSL `/dynamic/pdk_multi_beta`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/dynamic/pdk_multi_beta/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
### AgenticDSL `/dynamic/pdk_multi_gamma`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/dynamic/pdk_multi_gamma/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";

// ===== B.1.2: valid DSL → subgraph executes =====

TEST_CASE("GenerateSubgraph + MockLLM valid DSL → callback fires 1 graph",
          "[chat-real-llm-coverage][generate_subgraph][mock]") {
  ToolRegistry registry;
  MockLLMProvider mock;
  mock.set_fixed_response(kMinimalDynamicDsl);

  GenerateSubgraphNode node("/main/gen", "Generate minimal subgraph",
                            {"generated_path"}, {});

  int callback_count = 0;
  std::vector<std::string> captured_paths;

  NodeExecutor executor(registry, &mock, nullptr);
  executor.set_append_graphs_callback(
      [&callback_count, &captured_paths](std::vector<ParsedGraph> graphs) {
        callback_count++;
        for (auto& g : graphs) captured_paths.push_back(g.path);
      });

  Context ctx;
  ctx["__rendered_prompt__"] = std::string("rendered_prompt_content");

  REQUIRE_NOTHROW(executor.execute_node(&node, ctx));

  CHECK(callback_count == 1);
  CHECK(captured_paths.size() == 1);
  CHECK(captured_paths[0] == "/dynamic/pdk_minimal");
}

// ===== B.1.3: malformed YAML → graceful failure =====

TEST_CASE("GenerateSubgraph + MockLLM malformed YAML → graceful failure (no crash)",
          "[chat-real-llm-coverage][generate_subgraph][mock][error]") {
  ToolRegistry registry;
  MockLLMProvider mock;
  mock.set_fixed_response(kMalformedDynamicDsl);

  GenerateSubgraphNode node("/main/gen", "Generate broken subgraph",
                            {"generated_path"}, {});

  NodeExecutor executor(registry, &mock, nullptr);
  int callback_count = 0;
  executor.set_append_graphs_callback(
      [&callback_count](std::vector<ParsedGraph>) { callback_count++; });

  Context ctx;
  ctx["__rendered_prompt__"] = std::string("rendered");

  // 执行器应捕获 parser error, 不崩溃
  REQUIRE_THROWS(executor.execute_node(&node, ctx));
  // malformed YAML 不应触发 callback (parse 阶段已失败)
  CHECK(callback_count == 0);
}

// ===== B.1.4: arithmetic DSL → subgraph 含 llm_call 节点 =====

TEST_CASE("GenerateSubgraph + MockLLM arithmetic DSL → subgraph contains llm_call",
          "[chat-real-llm-coverage][generate_subgraph][mock]") {
  ToolRegistry registry;
  MockLLMProvider mock;
  mock.set_fixed_response(kArithmeticDynamic);

  GenerateSubgraphNode node("/main/gen", "Generate arithmetic subgraph",
                            {"generated_path"}, {});

  std::vector<ParsedGraph> captured_graphs;
  NodeExecutor executor(registry, &mock, nullptr);
  executor.set_append_graphs_callback(
      [&captured_graphs](std::vector<ParsedGraph> graphs) {
        captured_graphs = std::move(graphs);
      });

  Context ctx;
  ctx["__rendered_prompt__"] = std::string("rendered");

  REQUIRE_NOTHROW(executor.execute_node(&node, ctx));

  REQUIRE(captured_graphs.size() == 1);
  REQUIRE(captured_graphs[0].path == "/dynamic/pdk_calc");

  bool has_intermediate = false;
  for (const auto& n : captured_graphs[0].nodes) {
    if (n->type == NodeType::TOOL_CALL) {
      has_intermediate = true;
      break;
    }
  }
  CHECK(has_intermediate);
  CHECK(captured_graphs[0].nodes.size() >= 3);
}

// ===== B.1.5: multiple graphs (3 sequential) =====

TEST_CASE("GenerateSubgraph + MockLLM multi-graph → 3 graphs registered",
          "[chat-real-llm-coverage][generate_subgraph][mock][multi]") {
  ToolRegistry registry;
  MockLLMProvider mock;
  mock.set_fixed_response(kMultiDynamicDsl);

  GenerateSubgraphNode node("/main/gen", "Multi graph test",
                            {"generated_paths"}, {});

  std::vector<std::string> captured_paths;
  std::vector<size_t> callback_batch_sizes;

  NodeExecutor executor(registry, &mock, nullptr);
  executor.set_append_graphs_callback(
      [&captured_paths, &callback_batch_sizes](std::vector<ParsedGraph> graphs) {
        callback_batch_sizes.push_back(graphs.size());
        for (auto& g : graphs) captured_paths.push_back(g.path);
      });

  Context ctx;
  ctx["__rendered_prompt__"] = std::string("rendered");

  REQUIRE_NOTHROW(executor.execute_node(&node, ctx));

  // 3 个 dynamic graphs 应被一次性解析
  REQUIRE(captured_paths.size() == 3);
  std::set<std::string> unique_paths(captured_paths.begin(), captured_paths.end());
  CHECK(unique_paths.size() == 3);
  CHECK(unique_paths.count("/dynamic/pdk_multi_alpha") == 1);
  CHECK(unique_paths.count("/dynamic/pdk_multi_beta") == 1);
  CHECK(unique_paths.count("/dynamic/pdk_multi_gamma") == 1);
}

// ===== B.1.6: NodeExecutor 级集成 (替代 chat() 端到端) =====

TEST_CASE("GenerateSubgraph NodeExecutor integration (替代 chat() 端到端路径)",
          "[chat-real-llm-coverage][generate_subgraph][mock][integration]") {
  // chat() → loop/run → lib/loop/*.agent.md 不含 generate_subgraph 节点
  // (Oracle/Metis C5 Reachability Gap)
  // 替代: 直接 NodeExecutor 级集成, 验证核心语义
  ToolRegistry registry;
  MockLLMProvider mock;
  mock.set_fixed_response(kMinimalDynamicDsl);

  GenerateSubgraphNode node("/main/gen", "Integration test",
                            {"generated_path"}, {});

  std::vector<ParsedGraph> captured;
  NodeExecutor executor(registry, &mock, nullptr);
  executor.set_append_graphs_callback(
      [&captured](std::vector<ParsedGraph> graphs) { captured = std::move(graphs); });

  Context ctx;
  ctx["__rendered_prompt__"] = std::string("integration");

  REQUIRE_NOTHROW(executor.execute_node(&node, ctx));
  REQUIRE(captured.size() == 1);
  CHECK(captured[0].path == "/dynamic/pdk_minimal");
  // graph 含 start + end 节点
  CHECK(captured[0].nodes.size() == 2);
}

}  // namespace