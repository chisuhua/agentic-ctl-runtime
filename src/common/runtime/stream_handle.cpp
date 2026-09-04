// src/common/runtime/stream_handle.cpp
// ADR-0072 D1 阶段 B: 2 个 IStreamHandle 参考实现
// 设计依据: openspec/changes/adr-0072-d1-stream-runtime-semantics/design.md
//   - Decision 6: 双实现 (BufferedStreamHandle + CallbackStreamHandle)
//   - Decision 7: handle RAII 顺序 (mark inactive → clear queue → clear callback → notify_all CV waiters)
//   - Decision 8: error / EOF / Cancel 三态区分
#include "stream_handle.h"

#include <utility>

namespace agenticdsl {

// ============================================================
// BufferedStreamHandle: pull-only 累积 handle
// ============================================================

BufferedStreamHandle::~BufferedStreamHandle() {
  // RAII: mark inactive → 清队列 (析构时无需 notify, 因 push 是同步的)
  std::lock_guard<std::mutex> lock(mu_);
  active_ = false;
  while (!queue_.empty()) {
    queue_.pop();
  }
}

std::optional<std::string> BufferedStreamHandle::next(std::stop_token token) {
  std::lock_guard<std::mutex> lock(mu_);
  if (token.stop_requested()) {
    active_ = false;
    error_ = LLMError{LLMError::Code::Cancelled, "stop_token requested"};
    return std::nullopt;
  }
  if (!active_ && queue_.empty()) {
    return std::nullopt;  // close 且已 drain 完所有 chunks → EOF
  }
  if (queue_.empty()) {
    return std::nullopt;
  }
  std::string chunk = std::move(queue_.front());
  queue_.pop();
  return chunk;
}

bool BufferedStreamHandle::is_active() const {
  std::lock_guard<std::mutex> lock(mu_);
  return active_;
}

std::optional<LLMError> BufferedStreamHandle::error() const {
  std::lock_guard<std::mutex> lock(mu_);
  return error_;
}

void BufferedStreamHandle::push(std::string chunk) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!active_) {
    return;  // close 后 push 为 no-op (idempotent)
  }
  queue_.push(std::move(chunk));
}

void BufferedStreamHandle::close(std::optional<LLMError> err) {
  std::lock_guard<std::mutex> lock(mu_);
  active_ = false;
  if (err.has_value()) {
    error_ = err;
  }
  // Spec R1: close 后 consumer 可继续 drain 剩余 chunks, 直至 nullopt
}

// ============================================================
// CallbackStreamHandle: push + pull 双接口 (有界队列 + CV + callback)
// ============================================================

CallbackStreamHandle::CallbackStreamHandle(std::function<void(std::string)> on_chunk)
    : on_chunk_(std::move(on_chunk)) {}

CallbackStreamHandle::~CallbackStreamHandle() {
  // RAII: mark inactive → 清队列 → 清 callback → notify_all 唤醒阻塞 push 等待者
  std::lock_guard<std::mutex> lock(mu_);
  active_ = false;
  while (!queue_.empty()) {
    queue_.pop();
  }
  on_chunk_ = nullptr;  // 防 push 路径悬空调用
  cv_full_.notify_all();
}

void CallbackStreamHandle::set_callback(std::function<void(std::string)> on_chunk) {
  std::lock_guard<std::mutex> lock(mu_);
  on_chunk_ = std::move(on_chunk);
}

std::optional<std::string> CallbackStreamHandle::next(std::stop_token token) {
  std::lock_guard<std::mutex> lock(mu_);
  if (token.stop_requested()) {
    active_ = false;
    error_ = LLMError{LLMError::Code::Cancelled, "stop_token requested"};
    return std::nullopt;
  }
  if (!active_ && queue_.empty()) {
    return std::nullopt;  // close 且已 drain 完 → EOF
  }
  if (queue_.empty()) {
    return std::nullopt;
  }
  std::string chunk = std::move(queue_.front());
  queue_.pop();
  cv_full_.notify_one();
  return chunk;
}

bool CallbackStreamHandle::is_active() const {
  std::lock_guard<std::mutex> lock(mu_);
  return active_;
}

std::optional<LLMError> CallbackStreamHandle::error() const {
  std::lock_guard<std::mutex> lock(mu_);
  return error_;
}

void CallbackStreamHandle::push(std::string chunk) {
  std::unique_lock<std::mutex> lock(mu_);
  // 阻塞等待直到队列有空位或被析构唤醒
  cv_full_.wait(lock, [this] {
    return !active_ || queue_.size() < kCapacity;
  });
  if (!active_) {
    return;  // 析构后或已 close
  }
  queue_.push(std::move(chunk));
  // 在锁内同步触发 callback (push 路径, 用于 UI 实时渲染)
  if (on_chunk_) {
    // 注意: callback 在 mutex 持有时调用, 避免竞态
    // 若 callback 内部回调 next() 会死锁, 文档化限制
    on_chunk_(queue_.back());
  }
}

void CallbackStreamHandle::close(std::optional<LLMError> err) {
  std::lock_guard<std::mutex> lock(mu_);
  active_ = false;
  if (err.has_value()) {
    error_ = err;
  }
  // Spec R1: close 后 consumer 可继续 drain 剩余 chunks
  cv_full_.notify_all();
}

}  // namespace agenticdsl
