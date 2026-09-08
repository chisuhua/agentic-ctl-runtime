// tests/test_serializing_decorator.cpp
// 文件头注释
// 功能描述：SerializingDecorator 单元测试（Wave 1 #2 fix-cloud-adapter-multithreading）
//          4 cases: forward / serialize concurrent / stop_token / available_models
// 设计依据：openspec/changes/fix-cloud-adapter-multithreading/design.md §测试设计
// 作者：AgenticDSL Wave 1 #2
// 最后修改日期：2026-09-08

#include "catch_amalgamated.hpp"

#include "common/llm/serializing_decorator.h"
#include "common/llm/llm_types.h"
#include "common/llm/mock_provider.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace agenticdsl;

namespace {

// Recording mock provider — 记录 generate() 调用顺序 + 模拟并发可见性
// 用于验证 SerializingDecorator 确实让 inner 调用串行化
class RecordingMockProvider : public ILLMProvider {
 public:
  std::string last_model;
  int generate_calls = 0;
  std::atomic<int> active_calls{0};
  std::atomic<int> max_concurrent_observed{0};
  std::chrono::milliseconds simulate_delay{0};
  GenerationResult fixed;

  Result<GenerationResult, LLMError> generate(
      const GenerationRequest& req, std::stop_token token) override {
    int now_active = active_calls.fetch_add(1) + 1;
    int prev_max = max_concurrent_observed.load();
    while (now_active > prev_max &&
           !max_concurrent_observed.compare_exchange_weak(prev_max, now_active)) {
    }
    last_model = req.params.model;
    ++generate_calls;

    if (simulate_delay.count() > 0) {
      // 用 token-aware sleep 模拟长调用 (支持 cancellation)
      auto deadline = std::chrono::steady_clock::now() + simulate_delay;
      while (std::chrono::steady_clock::now() < deadline) {
        if (token.stop_requested()) {
          active_calls.fetch_sub(1);
          return Result<GenerationResult, LLMError>::failure(LLMError{
              LLMError::Code::Cancelled, "RecordingMock: cancelled"});
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }

    active_calls.fetch_sub(1);
    return Result<GenerationResult, LLMError>::success(fixed);
  }

  std::unique_ptr<IGenerationStream> generate_stream(
      const GenerationRequest&, std::stop_token) override {
    return nullptr;
  }

  std::vector<ILLMProvider::ModelInfo> available_models() const override {
    return {ILLMProvider::ModelInfo{"mock-serial", {}, 0, "test"}};
  }
};

}  // namespace

// === Test 1: SerializingDecorator forwards generate result (单 worker 透传) ===
TEST_CASE("SerializingDecorator forwards generate result",
          "[serializing_decorator][unit]") {
  auto inner = std::make_unique<RecordingMockProvider>();
  inner->fixed.text = "forwarded_response";
  RecordingMockProvider* raw = inner.get();
  SerializingDecorator decorator(std::move(inner), "test-forward");

  GenerationRequest req;
  req.prompt = "test prompt";
  auto result = decorator.generate(req, {});

  REQUIRE(result.has_value());
  REQUIRE(result.value().text == "forwarded_response");
  REQUIRE(raw->generate_calls == 1);
  // SerializingDecorator 是透明 pass-through, 不动 req.params.model
  // (model 清理属 fix-generation-request-model-default scope, 正交问题)
  REQUIRE(raw->last_model == "gpt-4o-mini");  // 默认值透传
  // cumulative_waiters 计所有进入 cv 的线程 (含未实际等待的), 单 worker = 1
  REQUIRE(decorator.cumulative_waiters() == 1);
}

// === Test 2: SerializingDecorator serializes concurrent generate calls ===
// 验证: 2 worker 同时提交, inner 收到调用是严格串行的 (max_concurrent_observed=1)
TEST_CASE("SerializingDecorator serializes concurrent generate calls",
          "[serializing_decorator][unit][concurrency]") {
  auto inner = std::make_unique<RecordingMockProvider>();
  inner->fixed.text = "concurrent_response";
  inner->simulate_delay = std::chrono::milliseconds(50);  // 模拟长调用
  RecordingMockProvider* raw = inner.get();
  SerializingDecorator decorator(std::move(inner), "test-concurrent");

  std::vector<std::thread> workers;
  std::atomic<int> success_count{0};
  const int kNumWorkers = 4;

  for (int i = 0; i < kNumWorkers; ++i) {
    workers.emplace_back([&]() {
      GenerationRequest req;
      req.prompt = "concurrent test";
      auto result = decorator.generate(req, {});
      if (result.has_value()) success_count.fetch_add(1);
    });
  }
  for (auto& t : workers) t.join();

  REQUIRE(success_count.load() == kNumWorkers);
  REQUIRE(raw->generate_calls == kNumWorkers);
  // 关键断言: inner 任何时刻最大并发数 = 1 (严格串行化)
  REQUIRE(raw->max_concurrent_observed.load() == 1);
  // 3 个 worker 曾在 cv 上等待 (第 1 个立即进入, 后 3 个等待)
  REQUIRE(decorator.cumulative_waiters() >= 3);
}

// === Test 3: SerializingDecorator honors stop_token during wait ===
// 验证: worker A 持有 inner (长 generate), worker B 在 cv 上等待时
//        stop_token.request_stop() → worker B 立即返回 Cancelled
TEST_CASE("SerializingDecorator honors stop_token during wait",
          "[serializing_decorator][unit][cancellation]") {
  auto inner = std::make_unique<RecordingMockProvider>();
  inner->fixed.text = "should_not_complete";
  inner->simulate_delay = std::chrono::milliseconds(500);  // 长 generate
  SerializingDecorator decorator(std::move(inner), "test-cancel");

  // Worker A 启动长调用持有 inner
  std::thread worker_a([&]() {
    GenerationRequest req;
    auto result = decorator.generate(req, {});
    REQUIRE(result.has_value());  // A 完成
  });

  // 等 A 进入 inner
  std::this_thread::sleep_for(std::chrono::milliseconds(30));

  // Worker B 提交并立即取消
  std::stop_source ss;
  std::thread worker_b([&]() {
    GenerationRequest req;
    auto result = decorator.generate(req, ss.get_token());
    // B 必须在 A 完成前立即返回 Cancelled
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == LLMError::Code::Cancelled);
  });

  // 取消 B 的 stop_token (B 已在 cv 上等待)
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  ss.request_stop();

  worker_a.join();
  worker_b.join();
  // A 完成 → B 立即取消 → invariant 保持
  REQUIRE(decorator.concurrent_count() == 0);
}

// === Test 4: SerializingDecorator available_models delegates ===
TEST_CASE("SerializingDecorator available_models delegates",
          "[serializing_decorator][unit]") {
  auto inner = std::make_unique<RecordingMockProvider>();
  SerializingDecorator decorator(std::move(inner), "test-models");

  auto models = decorator.available_models();
  REQUIRE(models.size() == 1);
  REQUIRE(models[0].name == "mock-serial");
}