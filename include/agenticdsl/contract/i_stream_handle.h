// include/agenticdsl/contract/i_stream_handle.h
// ADR-0072 D1 阶段 B: IStreamHandle L1 契约层
// 5 虚函数: 3 consumer pull + 2 producer push/close
// 独立于 IGenerationStream (per ADR-0001)
//
// 设计依据: openspec/changes/adr-0072-d1-stream-runtime-semantics/
//   - Decision 1: 独立契约层 (含 producer 侧 push/close per Oracle M1)
//   - Decision 2: next() 接受 std::stop_token (consumer-side closure per Oracle M3)
//   - Decision 6: 2 个参考实现 (Buffered + Callback)
//   - Decision 7: handle RAII 顺序 (mark inactive → clear queue → clear callback → notify_all CV)
//   - Decision 8: error/EOF/Cancel 三态区分
#pragma once

#include <optional>
#include <string>
#include <stop_token>

#include "common/llm/llm_types.h"  // LLMError

namespace agenticdsl {

// 流式回调契约层 — 5 虚函数
class IStreamHandle {
 public:
  virtual ~IStreamHandle() = default;

  // ===== Consumer 侧 (pull-based) =====

  // 从 handle 拉取下一个 chunk
  // 返回 std::nullopt 表示流结束 (active=false) 或被 stop_token 取消
  virtual std::optional<std::string> next(std::stop_token token) = 0;

  // 是否仍处于活跃状态 (next() 可能返回 chunk)
  virtual bool is_active() const = 0;

  // 流结束后查询错误状态:
  //   - nullopt = 正常 EOF
  //   - LLMError{Code::Cancelled} = 消费侧 stop_token 取消
  //   - 其他 Code = producer error (push 端 close(err) 或 push 路径异常)
  virtual std::optional<LLMError> error() const = 0;

  // ===== Producer 侧 (per Oracle M1 修复) =====

  // Producer 写入一个 chunk 到 handle
  // CallbackStreamHandle 会同步触发注册的 on_chunk 回调 (push 路径)
  // close() 之后调用为 no-op (idempotent)
  virtual void push(std::string chunk) = 0;

  // Producer 终止流
  // err=nullopt 表示正常 EOF
  // err=LLMError{Code::X, ...} 表示 producer error
  // close 后 is_active() == false 且后续 push() MUST 为 no-op
  virtual void close(std::optional<LLMError> err = std::nullopt) = 0;
};

}  // namespace agenticdsl
