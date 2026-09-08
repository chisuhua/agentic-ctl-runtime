// src/core/context_compactor.cpp
// 功能描述：上下文压缩器实现 (PIMPL-lite 完整实现)
//          ADR-0007 上下文压缩骨架
// 设计依据：ADR-0007 (上下文压缩) + .rddf/plans/context-compactor.md
// 作者：AgenticDSL context-compactor change
// 最后修改日期：2026-08-13

#include "core/context_compactor.h"

#include "agenticdsl/contract/bus_event.h"
#include "agenticdsl/contract/event_builder.h"
#include "agenticdsl/contract/iinteraction_bus.h"
#include "common/llm/llm_types.h"

#include <nlohmann/json.hpp>
#include <chrono>

namespace agenticdsl {

// PIMPL-lite 实现结构体
struct ContextCompactorImpl::Impl {
  size_t compact_threshold_tokens_;
  std::shared_ptr<ILLMProvider> llm_provider_;
  std::shared_ptr<IInteractionBus> event_bus_;

  Impl(size_t threshold,
       std::shared_ptr<ILLMProvider> llm,
       std::shared_ptr<IInteractionBus> bus)
      : compact_threshold_tokens_(threshold),
        llm_provider_(std::move(llm)),
        event_bus_(std::move(bus)) {}
};

ContextCompactorImpl::ContextCompactorImpl(
    size_t threshold,
    std::shared_ptr<ILLMProvider> llm,
    std::shared_ptr<IInteractionBus> bus)
    : impl_(std::make_unique<Impl>(threshold, std::move(llm), std::move(bus))) {}

ContextCompactorImpl::~ContextCompactorImpl() = default;

void ContextCompactorImpl::on_compact_before(const std::string& session_id,
                                             size_t tokens_before) {
  if (!impl_->event_bus_) return;
  impl_->event_bus_->emit(
      EventBuilder("context.compact.before")
          .args(nlohmann::json{{"session_id", session_id},
                                {"tokens_before", tokens_before}})
          .meta(nlohmann::json{{"component", "context_compactor"},
                               {"trace_id", session_id}})
          .build());
}

namespace {
constexpr const char* kSummaryPromptPrefix = "Summarize<200:\n";
}  // anonymous namespace

std::string ContextCompactorImpl::compact(const std::string& history_json,
                                         ILLMProvider& llm) {
  GenerationRequest req;
  req.prompt = std::string(kSummaryPromptPrefix) + history_json;
  // ⚠️ NOT redundant: LLMParams = LLMConfig 别名, 默认 model = "gpt-4o-mini"
  // (非空). 若不清空, CloudLLMAdapter L164 会拿默认遮蔽 factory 设置的真实
  // model → server 拒绝. 摘要场景 (Phase G 真实 LLM 启用时) 会被卡住.
  // 详见 openspec/changes/fix-generation-request-model-default/.
  req.params.model.clear();
  try {
    Result<GenerationResult, LLMError> result =
        llm.generate(req, std::stop_token{});
    if (!result.has_value()) {
      return "";
    }
    return result.value().text;
  } catch (const std::exception&) {
    return "";
  }
}

void ContextCompactorImpl::on_compact_after(const std::string& session_id,
                                            size_t tokens_before,
                                            size_t tokens_after) {
  if (!impl_->event_bus_) return;
  const double compression_ratio = tokens_before > 0
      ? static_cast<double>(tokens_after) / tokens_before
      : 1.0;
  impl_->event_bus_->emit(
      EventBuilder("context.compact.after")
          .args(nlohmann::json{{"session_id", session_id},
                                {"tokens_before", tokens_before},
                                {"tokens_after", tokens_after},
                                {"compression_ratio", compression_ratio}})
          .meta(nlohmann::json{{"component", "context_compactor"},
                               {"trace_id", session_id}})
          .build());
}

size_t ContextCompactorImpl::count_tokens(
    const std::string& context_json) const {
  // 字符计数代理: 4 chars ≈ 1 token (英文 LLM 经验值), 向上取整
  // 生产中应替换为 llm_provider_->count_tokens(text) 调用
  return (context_json.size() + 3) / 4;
}

bool ContextCompactorImpl::should_compact(size_t token_count) const {
  return token_count > impl_->compact_threshold_tokens_;
}

CompactionRecord ContextCompactorImpl::make_record(size_t tokens_before,
                                                 size_t tokens_after,
                                                 size_t summary_length) const {
  auto now = std::chrono::system_clock::now();
  auto epoch = std::chrono::system_clock::to_time_t(now);
  return CompactionRecord{tokens_before, tokens_after, summary_length,
                          std::to_string(epoch)};
}

// 工厂函数实现
std::unique_ptr<IContextCompactor> create_context_compactor(
    size_t compact_threshold_tokens,
    std::shared_ptr<ILLMProvider> llm_provider,
    std::shared_ptr<IInteractionBus> event_bus) {
  return std::make_unique<ContextCompactorImpl>(
      compact_threshold_tokens,
      std::move(llm_provider),
      std::move(event_bus));
}

}  // namespace agenticdsl
