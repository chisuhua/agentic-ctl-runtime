// src/core/types/tool_result.cpp
// 文件头注释
// 功能描述：ToolResult 的方法实现 (success/error 工厂、JSON 序列化/反序列化)。
//          Phase 1 Sprint 1a 扩展: ErrorCode enum + 4 个 optional 字段 (P2-P4)
// 设计依据：ADR-0023（ToolResult 标准化）+ plan §3 X 阶段
//          + openspec/changes/phase1-toolresult-standardization/specs/toolresult-p2-p4.md
// 作者：AgenticDSL Phase 0 / Track X + Phase 1 Sprint 1a
// 最后修改日期：2026-06-16
#include "core/types/tool_result.h"

#include <stdexcept>

namespace agenticdsl {

namespace {

// ErrorCode → string 映射 (REQ-TR-001 from_json 反序列化支持)
// 与 openspec/changes/phase1-toolresult-standardization/specs/toolresult-p2-p4.md
// REQ-TR-001 11 个值严格对应
const char* error_code_to_string(ErrorCode code) {
  switch (code) {
    case ErrorCode::Unknown:            return "Unknown";
    case ErrorCode::PermissionDenied:   return "PermissionDenied";
    case ErrorCode::PathViolation:      return "PathViolation";
    case ErrorCode::DangerousCommand:   return "DangerousCommand";
    case ErrorCode::ToolNotRegistered:  return "ToolNotRegistered";
    case ErrorCode::Retry:              return "Retry";
    case ErrorCode::Skip:               return "Skip";
    case ErrorCode::Abort:              return "Abort";
    case ErrorCode::Audit:              return "Audit";
    case ErrorCode::Timeout:            return "Timeout";
    case ErrorCode::ResourceExhausted:   return "ResourceExhausted";
    case ErrorCode::SandboxViolation:    return "SandboxViolation";
    case ErrorCode::MaxStepsExceeded:    return "MaxStepsExceeded";
    case ErrorCode::Crash:               return "Crash";
    case ErrorCode::BudgetExhausted:     return "BudgetExhausted";
    case ErrorCode::UnsupportedPlatform: return "UnsupportedPlatform";
    case ErrorCode::InvalidArg:          return "InvalidArg";
    case ErrorCode::InvalidParams:       return "InvalidParams";
    case ErrorCode::Cancelled:           return "Cancelled";
  }
  return "Unknown";  // 编译器要求
}

// string → ErrorCode 映射 (REQ-TR-001 from_json 反序列化支持)
// 容错: 未知字符串 → ErrorCode::Unknown
ErrorCode string_to_error_code(const std::string& s) {
  if (s == "PermissionDenied")   return ErrorCode::PermissionDenied;
  if (s == "PathViolation")      return ErrorCode::PathViolation;
  if (s == "DangerousCommand")   return ErrorCode::DangerousCommand;
  if (s == "ToolNotRegistered")  return ErrorCode::ToolNotRegistered;
  if (s == "Retry")              return ErrorCode::Retry;
  if (s == "Skip")               return ErrorCode::Skip;
  if (s == "Abort")              return ErrorCode::Abort;
  if (s == "Audit")              return ErrorCode::Audit;
  if (s == "Timeout")            return ErrorCode::Timeout;
  if (s == "ResourceExhausted")  return ErrorCode::ResourceExhausted;
  if (s == "SandboxViolation")   return ErrorCode::SandboxViolation;
  if (s == "MaxStepsExceeded")   return ErrorCode::MaxStepsExceeded;
  if (s == "Crash")              return ErrorCode::Crash;
  if (s == "BudgetExhausted")    return ErrorCode::BudgetExhausted;
  if (s == "UnsupportedPlatform") return ErrorCode::UnsupportedPlatform;
  if (s == "InvalidArg")         return ErrorCode::InvalidArg;
if (s == "InvalidParams")       return ErrorCode::InvalidParams;
  if (s == "Cancelled")           return ErrorCode::Cancelled;
  return ErrorCode::Unknown;
}

}  // namespace

ToolResult ToolResult::success(nlohmann::json d, nlohmann::json m) {
  ToolResult r;
  r.ok = true;
  r.data = std::move(d);
  r.meta = std::move(m);
  return r;
}

ToolResult ToolResult::error(ErrorCode code,
                             std::string msg,
                             nlohmann::json m) {
  ToolResult r;
  r.ok = false;
  // P2 ErrorCode enum 写入 error_code 字段
  r.error_code = code;
  // 同时双写 meta 保持 P1 兼容
  if (!m.is_object()) {
    m = nlohmann::json::object();
  }
  m["error_code"] = error_code_to_string(code);
  m["error_message"] = std::move(msg);
  r.meta = std::move(m);
  return r;
}

nlohmann::json ToolResult::to_json() const {
  nlohmann::json j;
  j["ok"] = ok;
  j["data"] = data;
  j["meta"] = meta;

  // P2-P4 optional 字段: 仅在有值时序列化
  if (error_code.has_value()) {
    j["error_code"] = error_code_to_string(*error_code);
  }
  if (latency_ms.has_value()) {
    j["latency_ms"] = *latency_ms;
  }
  if (trace_id.has_value()) {
    j["trace_id"] = *trace_id;
  }
  // ADR-0037 L2 因果链: parent_trace (causal-ordering-completion)
  if (parent_trace.has_value()) {
    j["parent_trace"] = *parent_trace;
  }
  if (metadata.has_value()) {
    j["metadata"] = *metadata;
  }
  return j;
}

ToolResult ToolResult::from_json(const nlohmann::json& j) {
  ToolResult r;
  // 缺失字段使用默认值 (ok=false, data/meta 为空对象)
  if (j.is_object()) {
    if (j.contains("ok") && j["ok"].is_boolean()) {
      r.ok = j["ok"].get<bool>();
    }
    if (j.contains("data")) {
      r.data = j["data"];
    }
    if (j.contains("meta")) {
      r.meta = j["meta"];
    }
    // P2: error_code (容错 string → ErrorCode 解析, 未知 → Unknown)
    if (j.contains("error_code") && j["error_code"].is_string()) {
      r.error_code = string_to_error_code(j["error_code"].get<std::string>());
    }
    // P3: latency_ms (uint64_t, 验证类型)
    // 接受 number_unsigned 或 number_integer (字面量 250 默认 number_integer)
    if (j.contains("latency_ms") && j["latency_ms"].is_number()) {
      r.latency_ms = j["latency_ms"].get<std::uint64_t>();
    }
    // P3: trace_id (string)
    if (j.contains("trace_id") && j["trace_id"].is_string()) {
      r.trace_id = j["trace_id"].get<std::string>();
    }
    // ADR-0037 L2 因果链: parent_trace (string, 缺值容错)
    if (j.contains("parent_trace") && j["parent_trace"].is_string()) {
      r.parent_trace = j["parent_trace"].get<std::string>();
    }
    // P3: metadata (json, 任意类型)
    if (j.contains("metadata")) {
      r.metadata = j["metadata"];
    }
  }
  // 非对象 JSON: 返回默认 (ok=false)
  return r;
}

}  // namespace agenticdsl
