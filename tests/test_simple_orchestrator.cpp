// tests/test_simple_orchestrator.cpp
// 文件头注释
// 功能描述：SimpleCognitiveOrchestrator 集成测试（5 个 TEST_CASE）
//          覆盖 mock 成功链路 / LLM 错误 / 工具不存在 / JSON 解析失败 / 端到端
// 设计依据：plan §11
// 作者：AgenticDSL Phase 0 / Track B
// 最后修改日期：2026-06-08

#include "catch_amalgamated.hpp"

#include "core/engine.h"
#include "core/types/tool_result.h"
#include "common/llm/mock_provider.h"
#include "common/llm/llm_types.h"
#include "agenticdsl/cognitive/simple_orchestrator.h"
#include "agenticdsl/contract/i_llm_provider_decorator.h"

#include <atomic>
#include <memory>
#include <string>

using namespace agenticdsl;

namespace {

// real-llm-core-coverage A.5 回归守卫 (Oracle P1-1):
// recording provider — 记录 generate() 收到的 req.params.model, 返回固定 JSON。
// 用途: CI (skip=1) 下唯一能捕获 model 遮蔽修复回归的确定性守卫。
class RecordingLLMProvider : public ILLMProvider {
 public:
  std::string last_model;
  int generate_calls = 0;
  bool last_token_stop_requested = false;  // fix-orchestrator-token-passthrough
  bool simulate_cancellation = false;  // fix-cancel-errorcode-semantics
  GenerationResult result;

  Result<GenerationResult, LLMError> generate(
      const GenerationRequest& req, std::stop_token token) override {
    last_model = req.params.model;
    last_token_stop_requested = token.stop_requested();
    ++generate_calls;
    if (simulate_cancellation) {
      return Result<GenerationResult, LLMError>::failure(
          LLMError{LLMError::Code::Cancelled, "simulated cancel"});
    }
    return Result<GenerationResult, LLMError>::success(result);
  }

  std::unique_ptr<IGenerationStream> generate_stream(
      const GenerationRequest&, std::stop_token) override {
    return nullptr;  // orchestrator 不用流式
  }

  std::vector<ModelInfo> available_models() const override { return {}; }
};

const std::string kEmptyDsl = R"(
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

} // namespace

// === Test 1: Mock 成功链路 ===
TEST_CASE("SimpleCognitiveOrchestrator mock success chain",
          "[cognitive][stage0]") {
  auto engine = DSLEngine::from_markdown(kEmptyDsl);
  engine->register_tool("echo", agenticdsl::ToolMetadata{"echo", "test", "test", agenticdsl::ToolCategory::ReadOnly, agenticdsl::LayerProfile::Workflow}, [](const std::unordered_map<std::string, std::string>& args) {
    return nlohmann::json{{"echoed", args.at("message")}};
  });
  auto* mock_unwrapped = dynamic_cast<MockLLMProvider*>(engine->get_llm_provider());
  if (!mock_unwrapped) { if (auto* d = dynamic_cast<ILLMProviderDecorator*>(engine->get_llm_provider())) mock_unwrapped = dynamic_cast<MockLLMProvider*>(d->inner()); }
  auto* mock = mock_unwrapped;
  REQUIRE(mock != nullptr);
  mock->set_fixed_response(R"({"tool":"echo","args":{"message":"hi"}})");

  SimpleCognitiveOrchestrator orch(&engine->get_tool_registry(),
                                   engine->get_llm_provider());

  bool done = false;
  ToolResult captured;
  orch.process("s1", [&](ToolResult r) {
    captured = std::move(r);
    done = true;
  });
  REQUIRE(done);
  REQUIRE(captured.ok);
  REQUIRE(captured.data["echoed"] == "hi");
  REQUIRE(captured.meta["tool_name"] == "echo");
}

// === Test 2: LLM 错误注入 ===
TEST_CASE("SimpleCognitiveOrchestrator LLM error injection",
          "[cognitive][stage0]") {
  auto engine = DSLEngine::from_markdown(kEmptyDsl);
  engine->register_tool("echo", agenticdsl::ToolMetadata{"echo", "test", "test", agenticdsl::ToolCategory::ReadOnly, agenticdsl::LayerProfile::Workflow}, [](const std::unordered_map<std::string, std::string>&) {
    return nlohmann::json::object();
  });
  auto* mock_unwrapped = dynamic_cast<MockLLMProvider*>(engine->get_llm_provider());
  if (!mock_unwrapped) { if (auto* d = dynamic_cast<ILLMProviderDecorator*>(engine->get_llm_provider())) mock_unwrapped = dynamic_cast<MockLLMProvider*>(d->inner()); }
  auto* mock = mock_unwrapped;
  REQUIRE(mock != nullptr);
  mock->set_simulate_error(LLMError::Code::NetworkError, "connection refused");

  SimpleCognitiveOrchestrator orch(&engine->get_tool_registry(),
                                   engine->get_llm_provider());
  bool done = false;
  ToolResult captured;
  orch.process("s2", [&](ToolResult r) {
    captured = std::move(r);
    done = true;
  });
  REQUIRE(done);
  REQUIRE_FALSE(captured.ok);
  REQUIRE(captured.meta["error_code"] == "Retry");
  REQUIRE(captured.meta["error_message"] == "connection refused");
}

// === Test 3: 工具不存在 ===
TEST_CASE("SimpleCognitiveOrchestrator tool not found",
          "[cognitive][stage0]") {
  auto engine = DSLEngine::from_markdown(kEmptyDsl);
  // 注意：不注册 echo 工具
  auto* mock_unwrapped = dynamic_cast<MockLLMProvider*>(engine->get_llm_provider());
  if (!mock_unwrapped) { if (auto* d = dynamic_cast<ILLMProviderDecorator*>(engine->get_llm_provider())) mock_unwrapped = dynamic_cast<MockLLMProvider*>(d->inner()); }
  auto* mock = mock_unwrapped;
  REQUIRE(mock != nullptr);
  mock->set_fixed_response(R"({"tool":"nonexistent","args":{}})");

  SimpleCognitiveOrchestrator orch(&engine->get_tool_registry(),
                                   engine->get_llm_provider());
  bool done = false;
  ToolResult captured;
  orch.process("s3", [&](ToolResult r) {
    captured = std::move(r);
    done = true;
  });
  REQUIRE(done);
  REQUIRE_FALSE(captured.ok);
  REQUIRE(captured.meta["error_code"] == "ToolNotRegistered");
}

// === Test 4: JSON 解析失败 ===
TEST_CASE("SimpleCognitiveOrchestrator JSON parse failure",
          "[cognitive][stage0]") {
  auto engine = DSLEngine::from_markdown(kEmptyDsl);
  engine->register_tool("echo", agenticdsl::ToolMetadata{"echo", "test", "test", agenticdsl::ToolCategory::ReadOnly, agenticdsl::LayerProfile::Workflow}, [](const std::unordered_map<std::string, std::string>&) {
    return nlohmann::json::object();
  });
  auto* mock_unwrapped = dynamic_cast<MockLLMProvider*>(engine->get_llm_provider());
  if (!mock_unwrapped) { if (auto* d = dynamic_cast<ILLMProviderDecorator*>(engine->get_llm_provider())) mock_unwrapped = dynamic_cast<MockLLMProvider*>(d->inner()); }
  auto* mock = mock_unwrapped;
  REQUIRE(mock != nullptr);
  mock->set_fixed_response("not valid json {");

  SimpleCognitiveOrchestrator orch(&engine->get_tool_registry(),
                                   engine->get_llm_provider());
  bool done = false;
  ToolResult captured;
  orch.process("s4", [&](ToolResult r) {
    captured = std::move(r);
    done = true;
  });
  REQUIRE(done);
  REQUIRE_FALSE(captured.ok);
  REQUIRE(captured.meta["error_code"] == "Unknown");
}

// === Test 5: 端到端（重复 Test 1 但验证完整 JSON 序列化）===
TEST_CASE("SimpleCognitiveOrchestrator end-to-end JSON output",
          "[cognitive][stage0][e2e]") {
  auto engine = DSLEngine::from_markdown(kEmptyDsl);
  engine->register_tool("echo", agenticdsl::ToolMetadata{"echo", "test", "test", agenticdsl::ToolCategory::ReadOnly, agenticdsl::LayerProfile::Workflow}, [](const std::unordered_map<std::string, std::string>& args) {
    return nlohmann::json{{"echoed", args.at("message")}, {"len", 5}};
  });
  auto* mock_unwrapped = dynamic_cast<MockLLMProvider*>(engine->get_llm_provider());
  if (!mock_unwrapped) { if (auto* d = dynamic_cast<ILLMProviderDecorator*>(engine->get_llm_provider())) mock_unwrapped = dynamic_cast<MockLLMProvider*>(d->inner()); }
  auto* mock = mock_unwrapped;
  REQUIRE(mock != nullptr);
  mock->set_fixed_response(R"({"tool":"echo","args":{"message":"hello"}})");

  SimpleCognitiveOrchestrator orch(&engine->get_tool_registry(),
                                   engine->get_llm_provider());
  bool done = false;
  ToolResult captured;
  orch.process("s5", [&](ToolResult r) {
    captured = std::move(r);
    done = true;
  });
  REQUIRE(done);
  REQUIRE(captured.ok);

  // 验证 to_json() 输出符合 ToolResult 格式
  auto j = captured.to_json();
  REQUIRE(j["ok"] == true);
  REQUIRE(j["data"]["echoed"] == "hello");
  REQUIRE(j["data"]["len"] == 5);
  REQUIRE(j["meta"]["tool_name"] == "echo");
}

// === Test 6 (real-llm-core-coverage A.5 / Oracle P1-1): model 遮蔽回归守卫 ===
// react_once 必须传空 params.model 给 provider (adapter 才能 fallback config_.model)。
// 若未来有人删掉 simple_orchestrator.cpp 的 req.params.model.clear(),
// LLMParams 默认 "gpt-4o-mini" 会再次遮蔽真实 provider 配置 — 本测试确定性拦截。
TEST_CASE("SimpleCognitiveOrchestrator passes empty model to provider",
          "[cognitive][stage0][realllm-guard]") {
  auto engine = DSLEngine::from_markdown(kEmptyDsl);
  engine->register_tool(
      "echo",
      agenticdsl::ToolMetadata{"echo", "test", "test",
                               agenticdsl::ToolCategory::ReadOnly,
                               agenticdsl::LayerProfile::Workflow},
      [](const std::unordered_map<std::string, std::string>& args) {
        return nlohmann::json{{"echoed", args.at("message")}};
      });
  auto recorder = std::make_unique<RecordingLLMProvider>();
  recorder->result.text = R"({"tool":"echo","args":{"message":"ok"}})";
  auto* raw = recorder.get();
  engine->set_llm_provider(std::move(recorder));  // 会被 decorate_provider 包装, raw 指针仍指向内层

  SimpleCognitiveOrchestrator orch(&engine->get_tool_registry(),
                                   engine->get_llm_provider());
  bool done = false;
  ToolResult captured;
  orch.process("s6", [&](ToolResult r) {
    captured = std::move(r);
    done = true;
  });
  REQUIRE(done);
  REQUIRE(captured.ok);
  REQUIRE(raw->generate_calls == 1);
  REQUIRE(raw->last_model.empty());  // 核心契约: model 必须为空 → adapter fallback
}

// === Test 7 (fix-orchestrator-token-passthrough): stop_token 透传至 llm_->generate ===
// react_once 必须把外部 stop_token 透传至 provider.generate (替换硬编码 {})。
// 回归守卫: RecordingLLMProvider 记录 last_token_stop_requested, 未来若有人回退
// token → {} 路径, pre-cancel 测试中 last_token_stop_requested 会变成 false → 拦截。
TEST_CASE("SimpleCognitiveOrchestrator forwards stop_token to LLM provider",
          "[cognitive][token][realllm-gap-fix]") {
  auto engine = DSLEngine::from_markdown(kEmptyDsl);
  engine->register_tool(
      "echo",
      agenticdsl::ToolMetadata{"echo", "test", "test",
                               agenticdsl::ToolCategory::ReadOnly,
                               agenticdsl::LayerProfile::Workflow},
      [](const std::unordered_map<std::string, std::string>& args) {
        return nlohmann::json{{"echoed", args.at("message")}};
      });
  auto recorder = std::make_unique<RecordingLLMProvider>();
  recorder->result.text = R"({"tool":"echo","args":{"message":"ok"}})";
  auto* raw = recorder.get();
  engine->set_llm_provider(std::move(recorder));

  SimpleCognitiveOrchestrator orch(&engine->get_tool_registry(),
                                   engine->get_llm_provider());

  // pre-cancel: 验证 token 透传 (非默认 token={}).
  // 原 react_once(token={}) 路径 → raw->last_token_stop_requested == false.
  // 新 react_once(token) 路径 → raw->last_token_stop_requested == true.
  std::stop_source ss;
  ss.request_stop();
  bool done = false;
  ToolResult captured;
  orch.process("s7", [&](ToolResult r) {
    captured = std::move(r);
    done = true;
  }, ss.get_token());

  REQUIRE(done);
  REQUIRE(raw->generate_calls == 1);
  REQUIRE(raw->last_token_stop_requested == true);  // 核心契约: pre-cancel 已透传
}

// === Test 8 (fix-cancel-errorcode-semantics): LLMError::Cancelled 映射到 ErrorCode::Cancelled ===
// RecordingLLMProvider.simulate_cancellation=true 触发 LLMError{Code::Cancelled}.
// orch.process 调用 llm_->generate 后, llm_error_to_error_code 必须返回 ErrorCode::Cancelled
// (而非 ErrorCode::Unknown). ToolResult.error_code 应等于 ErrorCode::Cancelled.
// 回归守卫: 未来回退 llm_error_to_error_code 的 Cancelled case → error_code 变 Unknown → 拦截.
TEST_CASE("LLMError::Cancelled maps to ErrorCode::Cancelled",
          "[cognitive][error-code][cancel][realllm-followup]") {
  auto engine = DSLEngine::from_markdown(kEmptyDsl);
  engine->register_tool(
      "echo",
      agenticdsl::ToolMetadata{"echo", "test", "test",
                               agenticdsl::ToolCategory::ReadOnly,
                               agenticdsl::LayerProfile::Workflow},
      [](const std::unordered_map<std::string, std::string>& args) {
        return nlohmann::json{{"echoed", args.at("message")}};
      });
  auto recorder = std::make_unique<RecordingLLMProvider>();
  recorder->simulate_cancellation = true;  // 触发 Cancelled 错误
  auto* raw = recorder.get();
  engine->set_llm_provider(std::move(recorder));

  SimpleCognitiveOrchestrator orch(&engine->get_tool_registry(),
                                   engine->get_llm_provider());

  bool done = false;
  ToolResult captured;
  orch.process("s8", [&](ToolResult r) {
    captured = std::move(r);
    done = true;
  });

  REQUIRE(done);
  REQUIRE_FALSE(captured.ok);
  REQUIRE(raw->generate_calls == 1);
  // 核心契约: Cancelled 错误映射到 ErrorCode::Cancelled, 而非 ErrorCode::Unknown
  REQUIRE(captured.error_code == ErrorCode::Cancelled);
}
