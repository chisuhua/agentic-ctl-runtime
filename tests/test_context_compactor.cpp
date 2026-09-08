// tests/test_context_compactor.cpp
// 功能描述：ContextCompactor 单元测试 (context-compactor Task 1)
//          TDD Step 4: 骨架验证测试
// 设计依据：ADR-0007 (上下文压缩) + .rddf/plans/context-compactor.md
// 作者：AgenticDSL context-compactor change
// 最后修改日期：2026-08-20 (P12: 迁移本地 MockBus → canonical test::MockBus)

#include "catch_amalgamated.hpp"

#include "core/context_compactor.h"
#include "agenticdsl/contract/iinteraction_bus.h"
#include "agenticdsl/contract/bus_event.h"
#include "common/llm/llm_types.h"
#include "agenticdsl/types/layered_context.h"
#include "test_helpers/mock_bus.h"

using namespace agenticdsl;

TEST_CASE("ContextCompactorImpl constructs with threshold") {
  ContextCompactorImpl compactor(4096, nullptr, nullptr);
  REQUIRE(compactor.should_compact(0) == false);  // stub, returns false
}

TEST_CASE("should_compact boundary: equals threshold is false, above is true") {
  ContextCompactorImpl compactor(4096, nullptr, nullptr);
  REQUIRE(compactor.should_compact(4096) == false);  // 等于阈值 → false
  REQUIRE(compactor.should_compact(4097) == true);   // 严格大于 → true
  REQUIRE(compactor.should_compact(0) == false);    // 零 → false
  REQUIRE(compactor.should_compact(4095) == false);  // 低于阈值 → false
}

TEST_CASE("ContextCompactorImpl should_compact returns false below threshold") {
  ContextCompactorImpl compactor(4096, nullptr, nullptr);
  REQUIRE(compactor.should_compact(4095) == false);
}

TEST_CASE("count_tokens uses ~4-char-per-token approximation") {
  ContextCompactorImpl compactor(4096, nullptr, nullptr);
  // 4 chars ≈ 1 token (英文 LLM 经验值)
  REQUIRE(compactor.count_tokens("") == 0);
  REQUIRE(compactor.count_tokens("1234") == 1);      // 4 chars = 1 token
  REQUIRE(compactor.count_tokens("12345678") == 2);  // 8 chars = 2 tokens
  REQUIRE(compactor.count_tokens("12345") == 2);     // 5 chars 向上取整 = 2 tokens
  REQUIRE(compactor.count_tokens(R"({"key": "value"})") > 1);  // JSON > 1 token
}

TEST_CASE("should_compact returns true strictly above threshold") {
  ContextCompactorImpl compactor(4096, nullptr, nullptr);
  REQUIRE(compactor.should_compact(5000) == true);   // 超阈值
  REQUIRE(compactor.should_compact(4097) == true);   // 严格大于
  REQUIRE(compactor.should_compact(4096) == false);  // 边界等于
  REQUIRE(compactor.should_compact(3000) == false);  // 远低
}

// Mock LLMProvider for testing compact()
class MockLLMForCompact : public ILLMProvider {
public:
  Result<GenerationResult, LLMError>
      generate(const GenerationRequest& req, std::stop_token) override {
    ++call_count;
    last_prompt = req.prompt;
    GenerationResult gr;
    gr.text = "[SUMMARY] " + req.prompt.substr(0, 30);
    gr.completion_tokens = 10;
    return Result<GenerationResult, LLMError>::success(std::move(gr));
  }
  std::unique_ptr<IGenerationStream>
      generate_stream(const GenerationRequest&, std::stop_token) override {
    return nullptr;
  }
  std::vector<ModelInfo> available_models() const override {
    return {ModelInfo{"mock", {}, 0, "mock"}};
  }
  int call_count = 0;
  std::string last_prompt;
};

class FailingLLMForCompact : public ILLMProvider {
public:
  Result<GenerationResult, LLMError>
      generate(const GenerationRequest&, std::stop_token) override {
    return Result<GenerationResult, LLMError>::failure(
        LLMError{LLMError::Code::NetworkError, "timeout"});
  }
  std::unique_ptr<IGenerationStream>
      generate_stream(const GenerationRequest&, std::stop_token) override {
    return nullptr;
  }
  std::vector<ModelInfo> available_models() const override { return {}; }
};

class ThrowingLLMForCompact : public ILLMProvider {
public:
  Result<GenerationResult, LLMError>
      generate(const GenerationRequest&, std::stop_token) override {
    throw std::runtime_error("boom");
  }
  std::unique_ptr<IGenerationStream>
      generate_stream(const GenerationRequest&, std::stop_token) override {
    return nullptr;
  }
  std::vector<ModelInfo> available_models() const override { return {}; }
};

TEST_CASE("compact returns LLM-generated summary via decorator chain") {
  MockLLMForCompact mock;
  ContextCompactorImpl compactor(4096, nullptr, nullptr);
  std::string result = compactor.compact("original history text", mock);
  REQUIRE(mock.call_count == 1);
  REQUIRE(mock.last_prompt.find("200") != std::string::npos);
  REQUIRE(result.rfind("[SUMMARY]", 0) == 0);
}

TEST_CASE("compact degrades gracefully when LLM returns no text") {
  FailingLLMForCompact mock;
  ContextCompactorImpl compactor(4096, nullptr, nullptr);
  // 不抛异常, 返回空串 (caller 决定下一步)
  std::string result = compactor.compact("orig", mock);
  REQUIRE(result.empty());
}

TEST_CASE("compact catches LLM exceptions and returns empty") {
  ThrowingLLMForCompact mock;
  ContextCompactorImpl compactor(4096, nullptr, nullptr);
  std::string result = compactor.compact("orig", mock);
  REQUIRE(result.empty());
}

// ============================================================
// fix-generation-request-model-default regression guard (Oracle P1-1)
// 验证 ContextCompactor::compact 必须传空 params.model 给 provider
// (让 CloudLLMAdapter L164 正确 fallback config_.model).
// 若未来有人删掉 context_compactor.cpp:66 的 req.params.model.clear(),
// LLMParams 默认 "gpt-4o-mini" 会再次遮蔽真实 model — 本测试确定性拦截.
// ============================================================
class RecordingLLMProvider_Compact : public ILLMProvider {
 public:
  std::string last_model;
  int generate_calls = 0;

  Result<GenerationResult, LLMError>
      generate(const GenerationRequest& req, std::stop_token) override {
    last_model = req.params.model;
    ++generate_calls;
    GenerationResult gr;
    gr.text = "[SUMMARY_OK]";
    gr.completion_tokens = 5;
    return Result<GenerationResult, LLMError>::success(std::move(gr));
  }
  std::unique_ptr<IGenerationStream>
      generate_stream(const GenerationRequest&, std::stop_token) override {
    return nullptr;
  }
  std::vector<ModelInfo> available_models() const override {
    return {ModelInfo{"mock", {}, 0, "mock"}};
  }
};

TEST_CASE("ContextCompactor compact passes empty model to provider",
          "[context_compactor][realllm-guard]") {
  RecordingLLMProvider_Compact recorder;
  ContextCompactorImpl compactor(4096, nullptr, nullptr);
  std::string result = compactor.compact("history to summarize", recorder);
  REQUIRE(recorder.generate_calls == 1);
  REQUIRE(recorder.last_model.empty());  // 核心契约: model 必须为空 → adapter fallback
  REQUIRE(result == "[SUMMARY_OK]");
}

// ADR-0068: EventBuilder V2 for context.compact events
TEST_CASE("on_compact_before uses EventBuilder with args+meta (ADR-0068)") {
  auto bus = std::make_shared<test::MockBus>();
  ContextCompactorImpl compactor(4096, nullptr, bus);
  compactor.on_compact_before("sess_xyz", 5000);
  REQUIRE(bus->events.size() == 1);
  const auto& e = bus->events[0];
  REQUIRE(e.topic == "context.compact.before");
  REQUIRE(e.payload.ok == true);
  REQUIRE(e.payload.data["session_id"] == "sess_xyz");
  REQUIRE(e.payload.data["tokens_before"] == 5000);
  // meta 应包含 component + trace_id (ADR-0068)
  REQUIRE(e.payload.meta.contains("component"));
  REQUIRE(e.payload.meta["component"] == "context_compactor");
}

TEST_CASE("on_compact_after includes tokens_before, tokens_after, compression_ratio") {
  auto bus = std::make_shared<test::MockBus>();
  ContextCompactorImpl compactor(4096, nullptr, bus);
  compactor.on_compact_after("sess_xyz", 5000, 1000);
  REQUIRE(bus->events.size() == 1);
  const auto& e = bus->events[0];
  REQUIRE(e.topic == "context.compact.after");
  REQUIRE(e.payload.data["session_id"] == "sess_xyz");
  REQUIRE(e.payload.data["tokens_before"] == 5000);
  REQUIRE(e.payload.data["tokens_after"] == 1000);
  REQUIRE(e.payload.data["compression_ratio"] == 0.2);
  REQUIRE(e.payload.meta.contains("component"));
  REQUIRE(e.payload.meta["component"] == "context_compactor");
}

// ADR-0069: 双层保留策略 — 原始消息存入 meta.original_messages
TEST_CASE("append_original appends to original_messages in meta") {
  LayeredContext ctx = LayeredContext::load(nlohmann::json{
      {"system", nlohmann::json::object()},
      {"recent", nlohmann::json::object()},
      {"working", nlohmann::json::object()},
      {"archive", nlohmann::json::object()},
      {"meta", nlohmann::json::object()}});
  ctx.append_original("first message");
  ctx.append_original("second message");
  REQUIRE(ctx.meta["original_messages"].is_array());
  REQUIRE(ctx.meta["original_messages"].size() == 2);
  REQUIRE(ctx.meta["original_messages"][0] == "first message");
  REQUIRE(ctx.meta["original_messages"][1] == "second message");
}

// ADR-0069: 双层保留策略 — 摘要替换 working 视图
TEST_CASE("set_working_view replaces working slot content") {
  LayeredContext ctx = LayeredContext::load(nlohmann::json{
      {"system", nlohmann::json::object()},
      {"recent", nlohmann::json::object()},
      {"working", nlohmann::json::object()},
      {"archive", nlohmann::json::object()},
      {"meta", nlohmann::json::object()}});
  ctx.set_working_view("summary text here");
  REQUIRE(ctx.working.contains("view"));
  REQUIRE(ctx.working["view"] == "summary text here");
}

// ADR-0069: 双层保留策略 — make_record 返回 CompactionRecord
TEST_CASE("make_record returns CompactionRecord with before/after/summary_length") {
  ContextCompactorImpl compactor(4096, nullptr, nullptr);
  CompactionRecord rec = compactor.make_record(5000, 1000, 800);
  REQUIRE(rec.tokens_before == 5000);
  REQUIRE(rec.tokens_after == 1000);
  REQUIRE(rec.summary_length == 800);
  REQUIRE(!rec.timestamp.empty());
}

// Wave 4: DSLEngine 集成 (Task 8)
#include "core/engine.h"

TEST_CASE("DSLEngine set_context_compactor stores unique_ptr") {
  DSLEngine engine;
  engine.set_context_compactor(
      create_context_compactor(4096, nullptr, nullptr));
  // No assertion needed — construction valid
  REQUIRE(true);
}

TEST_CASE("DSLEngine check_and_compact does nothing without compactor") {
  DSLEngine engine;
  // 没注册 compactor, 不崩
  LayeredContext ctx = LayeredContext::load(nlohmann::json{
      {"system", nlohmann::json::object()},
      {"recent", nlohmann::json::object()},
      {"working", nlohmann::json::object()},
      {"archive", nlohmann::json::object()},
      {"meta", nlohmann::json::object()}});
  engine.check_and_compact(ctx);  // should no-op
  REQUIRE(ctx.meta["original_messages"].is_null());
}
