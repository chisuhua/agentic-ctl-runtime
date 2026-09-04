// src/common/runtime/stream_handle.h
// ADR-0072 D1 阶段 B: 2 个 IStreamHandle 参考实现
// 设计依据: openspec/changes/adr-0072-d1-stream-runtime-semantics/design.md Decision 6
#pragma once

#include "agenticdsl/contract/i_stream_handle.h"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>

namespace agenticdsl {

// BufferedStreamHandle: pull-only 累积 handle
// Producer (NodeExecutor) 通过 push() 写入, Consumer 通过 next() 拉取
// 用于测试 + 简单累积场景
class BufferedStreamHandle : public IStreamHandle {
 public:
  BufferedStreamHandle() = default;
  ~BufferedStreamHandle() override;

  // Consumer 侧
  std::optional<std::string> next(std::stop_token token) override;
  bool is_active() const override;
  std::optional<LLMError> error() const override;

  // Producer 侧
  void push(std::string chunk) override;
  void close(std::optional<LLMError> err = std::nullopt) override;

 private:
  mutable std::mutex mu_;
  std::queue<std::string> queue_;
  bool active_ = true;
  std::optional<LLMError> error_;
};

// CallbackStreamHandle: push + pull 双接口
// 内部有界队列 (capacity=32), 满时 producer push() 通过 CV 阻塞
// push 时同步触发 on_chunk 回调 (用于 UI 实时渲染)
// 用于事件驱动消费者
class CallbackStreamHandle : public IStreamHandle {
 public:
  explicit CallbackStreamHandle(std::function<void(std::string)> on_chunk = nullptr);
  ~CallbackStreamHandle() override;

  std::optional<std::string> next(std::stop_token token) override;
  bool is_active() const override;
  std::optional<LLMError> error() const override;
  void push(std::string chunk) override;
  void close(std::optional<LLMError> err = std::nullopt) override;

  void set_callback(std::function<void(std::string)> on_chunk);

 private:
  static constexpr size_t kCapacity = 32;

  mutable std::mutex mu_;
  std::condition_variable cv_full_;  // 唤醒阻塞在满队列的 push 等待者 (析构时)
  std::queue<std::string> queue_;
  bool active_ = true;
  std::optional<LLMError> error_;
  std::function<void(std::string)> on_chunk_;
};

}  // namespace agenticdsl
