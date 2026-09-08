// tests/test_executor_with_mock_provider.cpp
// 功能描述：验证 DSLEngine 默认使用 MockLLMProvider
//          端到端集成测试（C₁.5）
// 设计依据：track-01-cloud-llm.md C₁.5、ADR-0001（ILLMProvider 流式接口）
// 作者：AgenticDSL Track C₁
// 最后修改日期：2026-06-08

#include "catch_amalgamated.hpp"
#include "core/engine.h"
#include "common/llm/mock_provider.h"
#include "agenticdsl/contract/i_llm_provider_decorator.h"
#include "common/llm/llama_adapter_provider.h"
#include "core/types/context.h"

#include <memory>
#include <string>

using namespace agenticdsl;

namespace {

// 简单的 DSL：只有 start/end 节点（不触发 LLM 调用）
const std::string kSimpleDsl = R"(
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

// 触发 generate_subgraph 的 DSL（需要 mock LLM 返回有效 DSL）
const std::string kGenerateSubgraphDsl = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/generate"]
  - id: generate
    type: generate_subgraph
    prompt_template: "Generate DSL for: {{ task }}"
    output_keys: ["generated_paths"]
    next: ["/main/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";

// Mock LLM 返回的有效 DSL（用于 generate_subgraph）
const std::string kMockDslResponse = R"(
### AgenticDSL `/dynamic/mock_generated`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  from_mock: "true"
# --- END AgenticDSL ---
```
)";

// fix-generation-request-model-default regression guard for GenerateSubgraphNode:
// 静态契约脚本 scripts/check-model-default-cleared.sh 覆盖全部 5 站点 (包括 node_executor.cpp:366).
// Unit-test 路径复杂: 需要构造 GenerateSubgraphNode + 完整 NodeExecutor 依赖 + 模板渲染路径,
// 投入产出比低于静态脚本. 已在 tests/test_context_compactor.cpp 加 ContextCompactor 的
// Recording Provider 单测作为 pattern 示范 (Oracle P1-1 风格).

} // namespace

// === 测试 1：DSLEngine 默认使用 MockLLMProvider ===
TEST_CASE("DSLEngine defaults to MockLLMProvider (no local LLM needed)", "[executor][mock_llm][stage0]") {
    auto engine = DSLEngine::from_markdown(kSimpleDsl);

    // 验证 provider 存在且类型为 MockLLMProvider
    ILLMProvider* provider = engine->get_llm_provider();
    REQUIRE(provider != nullptr);

    // MockLLMProvider 应该可以直接被 dynamic_cast
    auto* mock = dynamic_cast<MockLLMProvider*>(provider);
    if (!mock) { auto* _d = dynamic_cast<ILLMProviderDecorator*>(provider); if (_d) mock = dynamic_cast<MockLLMProvider*>(_d->inner()); }
    REQUIRE(mock != nullptr);
}

// === 测试 2：MockLLMProvider 通过 ILLMProvider 接口正常工作 ===
TEST_CASE("MockLLMProvider generate() returns fixed response via ILLMProvider interface",
          "[executor][mock_llm][stage0]") {
    MockLLMProvider mock;
    mock.set_fixed_response("mock_response_text");

    // 通过 ILLMProvider 接口调用
    GenerationRequest req;
    req.prompt = "test prompt";

    auto result = mock.generate(req, {});

    REQUIRE(result.has_value());
    REQUIRE(result.value().text == "mock_response_text");
    // 注：finish_reason 由调用方/LlamaAdapterProvider 设置，MockLLMProvider 保持空

    // 验证调用历史
    REQUIRE(mock.call_count() == 1);
}

// === 测试 3：set_llm_provider 替换默认 Mock ===
TEST_CASE("set_llm_provider replaces default MockLLMProvider", "[executor][mock_llm][stage0]") {
    auto engine = DSLEngine::from_markdown(kSimpleDsl);

    // 默认是 Mock
    ILLMProvider* _p2 = engine->get_llm_provider();
    auto* default_mock = dynamic_cast<MockLLMProvider*>(_p2);
    if (!default_mock) { if (auto* d = dynamic_cast<ILLMProviderDecorator*>(_p2)) default_mock = dynamic_cast<MockLLMProvider*>(d->inner()); }
    REQUIRE(default_mock != nullptr);

    // 注入自定义 provider
    auto custom_mock = std::make_unique<MockLLMProvider>();
    custom_mock->set_fixed_response("custom_response");
    engine->set_llm_provider(std::move(custom_mock));

    // 验证替换成功
    auto* new_provider = engine->get_llm_provider();
    REQUIRE(new_provider != nullptr);
    REQUIRE(new_provider != default_mock); // 应该是不同的指针

    // 调用验证
    GenerationRequest req;
    req.prompt = "test";
    auto result = new_provider->generate(req, {});
    REQUIRE(result.has_value());
    REQUIRE(result.value().text == "custom_response");
}

// === 测试 4：模拟错误注入 ===
TEST_CASE("MockLLMProvider simulates LLMError injection", "[executor][mock_llm][stage0]") {
    MockLLMProvider mock;
    mock.set_simulate_error(LLMError::Code::NetworkError, "simulated network failure");

    GenerationRequest req;
    req.prompt = "test";

    auto result = mock.generate(req, {});
    REQUIRE(!result.has_value());
    REQUIRE(result.error().code == LLMError::Code::NetworkError);
    REQUIRE(result.error().message == "simulated network failure");
    REQUIRE(result.error().retryable());
}

// === 测试 5：端到端 - 从 DSLEngine 到 ILLMProvider 的完整链路 ===
// 这个测试通过 GENERATE_SUBGRAPH 节点触发 LLM 调用
TEST_CASE("End-to-end: DSLEngine → TopoScheduler → ExecutionSession → NodeExecutor → MockLLMProvider",
          "[executor][mock_llm][stage0][e2e]") {
    auto engine = DSLEngine::from_markdown(kGenerateSubgraphDsl);

    // 获取默认 mock provider 并配置响应
    auto* mock_unwrapped = dynamic_cast<MockLLMProvider*>(engine->get_llm_provider());
  if (!mock_unwrapped) { if (auto* d = dynamic_cast<ILLMProviderDecorator*>(engine->get_llm_provider())) mock_unwrapped = dynamic_cast<MockLLMProvider*>(d->inner()); }
  auto* mock = mock_unwrapped;
    REQUIRE(mock != nullptr);
    mock->set_fixed_response(kMockDslResponse);

    // 准备上下文（generate_subgraph 需要 task 变量）
    // Sprint 20: LayeredContext fixture migration
    LayeredContext ctx;
    ctx.working["task"] = "test task";
    ctx.working["__rendered_prompt__"] = std::string("rendered prompt");

    // 运行引擎
    auto result = engine->run(ctx);

    REQUIRE(mock->call_count() > 0);
    REQUIRE(mock->call_history().back().prompt.find("test task") != std::string::npos);
}