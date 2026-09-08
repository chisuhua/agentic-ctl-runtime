// serializing_decorator.cpp
// 文件头注释
// 功能描述：SerializingDecorator 实现 — 详见 serializing_decorator.h
// 设计依据：openspec/changes/fix-cloud-adapter-multithreading/design.md §修复方案
// 作者：AgenticDSL Wave 1 #2
// 最后修改日期：2026-09-08

#include "common/llm/serializing_decorator.h"

#include <utility>

namespace agenticdsl {

SerializingDecorator::SerializingDecorator(std::unique_ptr<ILLMProvider> inner,
                                           std::string purpose)
    : inner_(std::move(inner)), purpose_(std::move(purpose)) {
  // inner_ 必非空 (调用方负责; 不检查避免在构造抛异常影响析构)
}

Result<GenerationResult, LLMError> SerializingDecorator::generate(
    const GenerationRequest& req, std::stop_token token) {
  std::unique_lock<std::mutex> lock(mutex_);
  ++active_waiters_;
  cumulative_waiters_.fetch_add(1, std::memory_order_relaxed);

  // 等待 cv: 串行化条件 = 无其他调用持有 inner, 或 token 取消
  cv_.wait(lock, [this, &token] {
    return concurrent_count_ == 0 || token.stop_requested();
  });

  if (token.stop_requested()) {
    // 取消: 释放 waiter 计数 + 唤醒其他 waiter (保持 invariant)
    --active_waiters_;
    cv_.notify_all();
    return Result<GenerationResult, LLMError>::failure(LLMError{
        LLMError::Code::Cancelled,
        "SerializingDecorator: cancelled while waiting for serializer"});
  }

  // 获得锁: 进入 inner call
  ++concurrent_count_;
  --active_waiters_;
  lock.unlock();  // 释放 mutex 允许其他 waiter 在 cv 上等待

  // 实际 inner call — 此时其他线程在 cv 上等待, 但 inner 已被本线程独占
  Result<GenerationResult, LLMError> result = inner_->generate(req, token);

  lock.lock();
  --concurrent_count_;
  cv_.notify_all();  // 唤醒下一个 waiter 进入 inner
  return result;
}

std::unique_ptr<IGenerationStream> SerializingDecorator::generate_stream(
    const GenerationRequest& req, std::stop_token token) {
  std::unique_lock<std::mutex> lock(mutex_);
  ++active_waiters_;
  cumulative_waiters_.fetch_add(1, std::memory_order_relaxed);

  cv_.wait(lock, [this, &token] {
    return concurrent_count_ == 0 || token.stop_requested();
  });

  if (token.stop_requested()) {
    --active_waiters_;
    cv_.notify_all();
    return nullptr;  // 取消: 返回 null 流 (调用方需检查)
  }

  ++concurrent_count_;
  --active_waiters_;
  lock.unlock();

  // 获取 inner stream (同步部分串行化)
  std::unique_ptr<IGenerationStream> stream =
      inner_->generate_stream(req, token);

  lock.lock();
  --concurrent_count_;
  cv_.notify_all();
  return stream;
}

std::vector<ILLMProvider::ModelInfo> SerializingDecorator::available_models() const {
  // 查询方法, 无需串行化 (不修改状态)
  // 但仍加锁避免与 generate concurrent (inner 实现可能非线程安全)
  std::lock_guard<std::mutex> lock(mutex_);
  return inner_->available_models();
}

int SerializingDecorator::concurrent_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return concurrent_count_;
}

std::size_t SerializingDecorator::active_waiters() const {
  return active_waiters_.load(std::memory_order_relaxed);
}

std::size_t SerializingDecorator::cumulative_waiters() const {
  return cumulative_waiters_.load(std::memory_order_relaxed);
}

}  // namespace agenticdsl