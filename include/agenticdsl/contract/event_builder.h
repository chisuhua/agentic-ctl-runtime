// include/agenticdsl/contract/event_builder.h
// 功能描述：统一 BusEvent 构造方言 (ADR-0068) + 扩展支持 operation-result events
//          args 放 schema 必填业务字段, meta 放 trace_id/session_id/debug 上下文
//          timestamp 由 build() 自动填充 steady_clock::now()
//          V2 扩展 (2026-08-03 promote-event-builder-fulltoolresult-support):
//            - full-payload 构造函数: EventBuilder(topic, ToolResult) 接管 7 字段
//            - 5 个 P2-P4 optional setters: ok / error_code / latency_ms / trace_id / metadata
//            - 覆盖 §决策 7 Operation-Result 事件 (8 处 raw BusEvent 迁移)
// 设计依据：openspec/changes/adr-0068-event-emission-contract/design.md Decision 1
//          + openspec/changes/promote-event-builder-fulltoolresult-support/
// 作者：HydraForge ADR-0068 + V2 extension
// 最后修改日期：2026-08-03
#pragma once

#include "agenticdsl/contract/bus_event.h"
#include "core/types/tool_result.h"
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace agenticdsl {

// 链式构造器，统一三种 BusEvent 构造方言：
// 1) 工具事件：EventBuilder("tool.x").args({...}).meta({...}).build()
// 2) 审计事件：EventBuilder("audit.x").meta({...}).build()
// 3) 生命周期事件：EventBuilder("lifecycle.x").args({...}).build()
//
// V2 扩展: full-payload constructor + 5 setters 覆盖 operation-result events
//   - EventBuilder(topic, ToolResult) 直接接管完整 ToolResult (含 ok=false/error_code/latency_ms/trace_id/metadata)
//   - .ok(false) 显式控制 payload.ok
//   - .error_code(ErrorCode) / .latency_ms(uint64_t) / .trace_id(string) / .metadata(json)
//     单独设置 P2-P4 optional 字段 (telemetry 场景, 手搓 full ToolResult)
class EventBuilder {
 public:
  // V1: 构造函数 #1 (telemetry events: 默认 ok=true + 空 data/meta)
  explicit EventBuilder(std::string topic)
      : topic_(std::move(topic)),
        payload_(ToolResult::success(nlohmann::json::object(),
                                      nlohmann::json::object())) {}

  // V2: 构造函数 #2 (operation-result events: 接管完整 ToolResult 含 7 字段)
  // 用于 §5 ADR-0068 第七/八节 8 处 raw BusEvent 迁移场景
  // ToolResult 通过 const 引用 + 拷贝 (避免悬空, payload_ 必须是可独立持有)
  explicit EventBuilder(std::string topic, ToolResult payload)
      : topic_(std::move(topic)),
        payload_(std::move(payload)) {}

  // === V1 setters (历史 API, 保持不变) ===
  EventBuilder& args(nlohmann::json args) {
    payload_.data = std::move(args);
    return *this;
  }

  EventBuilder& meta(nlohmann::json meta) {
    payload_.meta = std::move(meta);
    return *this;
  }

  // === V2 setters (operation-result fields) ===
  // 设置 payload.ok 字段 (默认 true, 用于显式标记失败事件)
  EventBuilder& ok(bool v) {
    payload_.ok = v;
    return *this;
  }

  // 设置 payload.error_code (P2 REQ-TR-001)
  EventBuilder& error_code(ErrorCode code) {
    payload_.error_code = code;
    return *this;
  }

  // 设置 payload.latency_ms (P3 REQ-TR-002)
  EventBuilder& latency_ms(std::uint64_t ms) {
    payload_.latency_ms = ms;
    return *this;
  }

  // 设置 payload.trace_id (P3 REQ-TR-003)
  EventBuilder& trace_id(std::string tid) {
    payload_.trace_id = std::move(tid);
    return *this;
  }

  // 设置 payload.parent_trace (ADR-0037 L2 因果链, causal-ordering-completion)
  // 用于走无 ToolResult 构造路径 (e.g. cognitive.task.started) 时显式声明
  // 当前事件的 parent trace_id, consumer 用 causal_order() 判定因果关系.
  EventBuilder& parent_trace(std::string tid) {
    payload_.parent_trace = std::move(tid);
    return *this;
  }

  // 设置 payload.metadata (P4 REQ-TR-004, 与 meta 不同的字段)
  EventBuilder& metadata(nlohmann::json m) {
    payload_.metadata = std::move(m);
    return *this;
  }

  // 构造 BusEvent (timestamp 自动填充)
  BusEvent build() const {
    return BusEvent{topic_, payload_, std::chrono::steady_clock::now()};
  }

 private:
  std::string topic_;
  ToolResult payload_;
};

}  // namespace agenticdsl
