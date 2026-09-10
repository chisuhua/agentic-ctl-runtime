// mock_provider.cpp
// 功能描述：Mock LLM Provider 实现
//          支持队列 / 固定响应 / 错误注入 / 延迟模拟 / 流式 token
// 设计依据：track-01-cloud-llm.md M1.9
// 作者：AgenticDSL Track 0.1
// 最后修改日期：2026-06-07

#include "mock_provider.h"

#include <chrono>
#include <stop_token>
#include <thread>
#include <utility>

namespace agenticdsl {

// =====================================================================
// 内部流实现
// =====================================================================
class MockLLMProvider::MockGenerationStream : public IGenerationStream {
public:
  MockGenerationStream(std::vector<std::string> tokens)
      : tokens_(std::move(tokens)), active_(true) {}

  std::optional<std::string> next(std::stop_token token) override {
    if (!active_) {
      return std::nullopt;
    }
    if (token.stop_requested()) {
      active_ = false;
      return std::nullopt;
    }
    if (index_ < tokens_.size()) {
      return tokens_[index_++];
    }
    active_ = false;
    return std::nullopt;
  }

  bool is_active() const override { return active_; }

private:
  std::vector<std::string> tokens_;
  size_t index_ = 0;
  bool active_;
};

// =====================================================================
// MockLLMProvider 实现
// =====================================================================

void MockLLMProvider::enqueue_response(const std::string& content) {
  GenerationResult r;
  r.text = content;
  response_queue_.push(std::move(r));
}

void MockLLMProvider::enqueue_response(GenerationResult result) {
  response_queue_.push(std::move(result));
}

void MockLLMProvider::set_fixed_response(const std::string& content) {
  GenerationResult r;
  r.text = content;
  fixed_response_ = std::move(r);
}

void MockLLMProvider::set_fixed_response(GenerationResult result) {
  fixed_response_ = std::move(result);
}

void MockLLMProvider::set_stream_tokens(std::vector<std::string> tokens) {
  stream_tokens_ = std::move(tokens);
}

void MockLLMProvider::set_simulate_error(LLMError::Code code,
                                          const std::string& message) {
  simulated_error_ = LLMError{code, message};
}

void MockLLMProvider::set_simulate_delay(std::chrono::milliseconds delay) {
  delay_ = delay;
}

void MockLLMProvider::reset() {
   // std::queue 无 clear 方法（C++20 之前），逐个弹出
   while (!response_queue_.empty()) {
     response_queue_.pop();
   }
   fixed_response_.reset();
   stream_tokens_.clear();
   {
     std::lock_guard<std::mutex> lock(history_mutex_);
     history_.clear();
   }
   simulated_error_.reset();
   delay_ = std::chrono::milliseconds{0};
   test_models_.clear();
}

void MockLLMProvider::set_available_models(std::vector<ModelInfo> models) {
   test_models_ = std::move(models);
}

GenerationResult MockLLMProvider::next_response() {
  if (!response_queue_.empty()) {
    auto r = response_queue_.front();
    response_queue_.pop();
    return r;
  }
  if (fixed_response_.has_value()) {
    return *fixed_response_;
  }
  return GenerationResult{};  // 空响应
}

Result<GenerationResult, LLMError>
MockLLMProvider::generate(const GenerationRequest& req, std::stop_token token) {
  // 记录调用历史 — history_mutex_ 保护 (见 header 注释).
  {
    std::lock_guard<std::mutex> lock(history_mutex_);
    history_.push_back(req);
  }

  // 模拟延迟
  if (delay_.count() > 0) {
    // 分段 sleep 以响应取消
    auto remaining = delay_;
    const auto step = std::chrono::milliseconds(50);
    while (remaining.count() > 0) {
      if (token.stop_requested()) {
        return Result<GenerationResult, LLMError>::failure(
            LLMError{LLMError::Code::Cancelled, "Cancelled during delay"});
      }
      auto sleep_for = std::min(step, remaining);
      std::this_thread::sleep_for(sleep_for);
      remaining -= sleep_for;
    }
  }

  // 错误注入
  if (simulated_error_.has_value()) {
    return Result<GenerationResult, LLMError>::failure(*simulated_error_);
  }

  return Result<GenerationResult, LLMError>::success(next_response());
}

std::unique_ptr<IGenerationStream>
MockLLMProvider::generate_stream(const GenerationRequest& req,
                                  std::stop_token token) {
  // 记录调用历史 — history_mutex_ 保护 (见 header 注释).
  {
    std::lock_guard<std::mutex> lock(history_mutex_);
    history_.push_back(req);
  }

  // 模拟延迟（流式接口不支持错误注入错误返回，由调用方检测空流）
  if (delay_.count() > 0) {
    std::this_thread::sleep_for(delay_);
  }

  // 使用 stream_tokens_（若已设置），否则按 prompt 拆词
  std::vector<std::string> tokens;
  if (!stream_tokens_.empty()) {
    tokens = stream_tokens_;
  } else {
    tokens = split(req.prompt);
  }

  return std::make_unique<MockGenerationStream>(std::move(tokens));
}

// === Phase 1 Sprint 0 新增 (K1 Plugin Stub 验证) ===
// MockLLMProvider 默认注册 1 个 mock 模型, 供 Plugin Stub 路由决策
// Sprint 5 PluginLoader 实现后, 真实 plugin 可读取此列表并应用 Policy
std::vector<ILLMProvider::ModelInfo> MockLLMProvider::available_models() const {
  if (!test_models_.empty()) {
    return test_models_;
  }
  // 默认返回 mock-llm-v1 (保持向后兼容)
  return {
      ModelInfo("mock-llm-v1",
                {ModelCapability::Chat, ModelCapability::ToolUse},
                4096,
                "mock")
  };
 }

} // namespace agenticdsl
