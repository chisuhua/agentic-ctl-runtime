// tests/test_tool_result.cpp
// 文件头注释
// 功能描述：ToolResult MVP 信封的单元测试。
//          覆盖 success/error 工厂、JSON 序列化/反序列化、缺失字段处理。
//          Phase 1 Sprint 1a (S1a.T4) 新增: ErrorCode 分类、latency_ms、trace_id、metadata。
// 设计依据：plan §4 X 阶段单元测试 + openspec REQ-TR-001/002/003/004
// 作者：AgenticDSL Phase 0 / Track X + Phase 1 Sprint 1a
// 最后修改日期：2026-06-16

#include "catch_amalgamated.hpp"

#include "core/types/tool_result.h"

using namespace agenticdsl;

TEST_CASE("ToolResult success factory", "[types][tool_result]") {
  nlohmann::json data = {{"echoed", "hello"}, {"count", 3}};
  auto r = ToolResult::success(data);

  REQUIRE(r.ok == true);
  REQUIRE(r.data == data);
  // meta 应默认为空对象（不是 null）
  REQUIRE(r.meta.is_object());
  REQUIRE(r.meta.empty());
}

TEST_CASE("ToolResult error factory", "[types][tool_result]") {
  auto r = ToolResult::error(ErrorCode::Retry, "timeout");

  REQUIRE(r.ok == false);
  // error_code 与 error_message 必须注入到 meta
  REQUIRE(r.meta.contains("error_code"));
  REQUIRE(r.meta["error_code"] == "Retry");
  REQUIRE(r.meta.contains("error_message"));
  REQUIRE(r.meta["error_message"] == "timeout");
  // data 应保持为空对象（错误结果不应携带工具输出）
  REQUIRE(r.data.is_object());
  REQUIRE(r.data.empty());
}

TEST_CASE("ToolResult JSON roundtrip", "[types][tool_result]") {
  // 1) success 往返
  ToolResult success_r;
  success_r.ok = true;
  success_r.data = {{"k", "v"}, {"n", 42}};
  success_r.meta = {{"trace_id", "abc-123"}};

  auto j1 = success_r.to_json();
  auto roundtrip1 = ToolResult::from_json(j1);
  REQUIRE(roundtrip1.ok == success_r.ok);
  REQUIRE(roundtrip1.data == success_r.data);
  REQUIRE(roundtrip1.meta == success_r.meta);

  // 2) error 往返
  ToolResult error_r = ToolResult::error(ErrorCode::Unknown, "bad json");
  auto j2 = error_r.to_json();
  auto roundtrip2 = ToolResult::from_json(j2);
  REQUIRE(roundtrip2.ok == error_r.ok);
  REQUIRE(roundtrip2.meta["error_code"] == "Unknown");
  REQUIRE(roundtrip2.meta["error_message"] == "bad json");
}

TEST_CASE("ToolResult handles missing fields gracefully",
          "[types][tool_result]") {
  // 完整缺失（空对象） -> ok=false
  nlohmann::json empty_j = nlohmann::json::object();
  auto r1 = ToolResult::from_json(empty_j);
  REQUIRE(r1.ok == false);
  REQUIRE((r1.data.is_null() || r1.data.is_object()));
  REQUIRE((r1.meta.is_null() || r1.meta.is_object()));

  // 仅 ok 字段
  nlohmann::json partial_j = {{"ok", true}};
  auto r2 = ToolResult::from_json(partial_j);
  REQUIRE(r2.ok == true);
  // data/meta 缺失时回退默认（空对象）
  REQUIRE(r2.data.is_object());
  REQUIRE(r2.meta.is_object());

  // 非对象 JSON（数组）-> 应返回默认值
  nlohmann::json arr_j = nlohmann::json::array();
  auto r3 = ToolResult::from_json(arr_j);
  REQUIRE(r3.ok == false);
}

// === Phase 1 Sprint 1a (S1a.T4) 新增测试 ===

// REQ-TR-001: ErrorCode 分类 — Retry / Skip / Abort / PermissionDenied / Timeout 等
TEST_CASE("ToolResult P2 ErrorCode classification", "[types][tool_result][phase1]") {
  SECTION("Retry error_code") {
    auto r = ToolResult::error(ErrorCode::Retry, "network blip");
    REQUIRE(r.ok == false);
    REQUIRE(r.error_code.has_value());
    REQUIRE(r.error_code.value() == ErrorCode::Retry);
    // meta 双写保留 P1 兼容
    REQUIRE(r.meta["error_code"] == "Retry");
    REQUIRE(r.meta["error_message"] == "network blip");
  }

  SECTION("Abort error_code") {
    auto r = ToolResult::error(ErrorCode::Abort, "fatal");
    REQUIRE(r.error_code.value() == ErrorCode::Abort);
    REQUIRE(r.meta["error_code"] == "Abort");
  }

  SECTION("PermissionDenied error_code (P1)") {
    auto r = ToolResult::error(ErrorCode::PermissionDenied, "no access");
    REQUIRE(r.error_code.value() == ErrorCode::PermissionDenied);
    REQUIRE(r.meta["error_code"] == "PermissionDenied");
  }

  SECTION("Timeout error_code (P2)") {
    auto r = ToolResult::error(ErrorCode::Timeout, "60s elapsed");
    REQUIRE(r.error_code.value() == ErrorCode::Timeout);
  }

  SECTION("Unknown default enum value") {
    // Default-constructed error_code should be nullopt, not Unknown
    ToolResult r;
    REQUIRE_FALSE(r.error_code.has_value());
  }
}

// REQ-TR-002: latency_ms 字段 — 显式设置 + JSON 序列化
TEST_CASE("ToolResult P3 latency_ms field", "[types][tool_result][phase1]") {
  ToolResult r = ToolResult::success({{"k", "v"}});
  REQUIRE_FALSE(r.latency_ms.has_value());

  r.latency_ms = 12345;
  REQUIRE(r.latency_ms.value() == 12345);

  // JSON 序列化往返
  auto j = r.to_json();
  REQUIRE(j.contains("latency_ms"));
  REQUIRE(j["latency_ms"] == 12345);

  auto roundtrip = ToolResult::from_json(j);
  REQUIRE(roundtrip.latency_ms.has_value());
  REQUIRE(roundtrip.latency_ms.value() == 12345);

  // 缺失时不写入 JSON
  ToolResult r2 = ToolResult::success({});
  auto j2 = r2.to_json();
  REQUIRE_FALSE(j2.contains("latency_ms"));
}

// REQ-TR-003: trace_id 字段 — 设置 + 透传
TEST_CASE("ToolResult P3 trace_id field", "[types][tool_result][phase1]") {
  ToolResult r = ToolResult::success({});
  r.trace_id = "trace-abc-123";

  REQUIRE(r.trace_id.has_value());
  REQUIRE(r.trace_id.value() == "trace-abc-123");

  auto j = r.to_json();
  REQUIRE(j["trace_id"] == "trace-abc-123");

  auto roundtrip = ToolResult::from_json(j);
  REQUIRE(roundtrip.trace_id.has_value());
  REQUIRE(roundtrip.trace_id.value() == "trace-abc-123");

  // 缺失时 roundtrip 也缺失
  ToolResult r2 = ToolResult::success({});
  auto j2 = r2.to_json();
  REQUIRE_FALSE(j2.contains("trace_id"));
  auto r2_rt = ToolResult::from_json(j2);
  REQUIRE_FALSE(r2_rt.trace_id.has_value());
}

// REQ-TR-004: metadata 与 meta 共存 — 两者独立保留
TEST_CASE("ToolResult P3 metadata coexists with meta", "[types][tool_result][phase1]") {
  ToolResult r = ToolResult::success(
      {{"data_k", "data_v"}},
      {{"meta_k", "meta_v"}});  // P1 meta
  r.metadata = {{"caller", "CognitiveWorker"}, {"attempt", 2}};  // P3 metadata

  REQUIRE(r.meta["meta_k"] == "meta_v");      // P1 meta 不受影响
  REQUIRE(r.metadata.has_value());
  REQUIRE(r.metadata.value()["caller"] == "CognitiveWorker");
  REQUIRE(r.metadata.value()["attempt"] == 2);

  auto j = r.to_json();
  REQUIRE(j.contains("meta"));
  REQUIRE(j.contains("metadata"));
  REQUIRE(j["meta"]["meta_k"] == "meta_v");
  REQUIRE(j["metadata"]["caller"] == "CognitiveWorker");

  auto roundtrip = ToolResult::from_json(j);
  REQUIRE(roundtrip.meta["meta_k"] == "meta_v");
  REQUIRE(roundtrip.metadata.has_value());
  REQUIRE(roundtrip.metadata.value()["attempt"] == 2);
}

// REQ-TR-001..004: from_json 全字段往返 — 验证信封模式解析
TEST_CASE("ToolResult from_json parses envelope with all P2-P4 fields",
          "[types][tool_result][phase1]") {
  nlohmann::json env = {
    {"ok", false},
    {"data", {{"partial", "result"}}},
    {"meta", {{"extra", "info"}}},
    {"error_code", "Retry"},
    {"latency_ms", 250},
    {"trace_id", "trace-xyz"},
    {"metadata", {{"retry_after_ms", 1000}}}
  };

  auto r = ToolResult::from_json(env);
  REQUIRE(r.ok == false);
  REQUIRE(r.data["partial"] == "result");
  REQUIRE(r.meta["extra"] == "info");
  REQUIRE(r.error_code.value() == ErrorCode::Retry);
  REQUIRE(r.latency_ms.value() == 250);
  REQUIRE(r.trace_id.value() == "trace-xyz");
  REQUIRE(r.metadata.value()["retry_after_ms"] == 1000);
}

// REQ-TR-001: from_json 容错 — 未知 error_code 字符串降级为 Unknown
TEST_CASE("ToolResult from_json tolerates unknown error_code strings",
          "[types][tool_result][phase1]") {
  nlohmann::json env = {
    {"ok", false},
    {"error_code", "SomeUnknownCode"}
  };

  auto r = ToolResult::from_json(env);
  REQUIRE(r.error_code.has_value());
  REQUIRE(r.error_code.value() == ErrorCode::Unknown);
}

// REQ-TR-002: from_json 类型校验 — latency_ms 非 unsigned 整数忽略
TEST_CASE("ToolResult from_json ignores invalid latency_ms type",
          "[types][tool_result][phase1]") {
  nlohmann::json env = {
    {"ok", true},
    {"latency_ms", "not_a_number"}  // 字符串而非 uint64
  };

  auto r = ToolResult::from_json(env);
  REQUIRE(r.ok == true);
  REQUIRE_FALSE(r.latency_ms.has_value());  // 类型不匹配, 忽略
}

// === Test (fix-cancel-errorcode-semantics P3): ErrorCode::Cancelled round-trip ===
// ToolResult::to_json / from_json 对 ErrorCode::Cancelled 必须双向对称 (透过
// error_code_to_string + string_to_error_code 实现, 测试公共 API 验证即可).
// 回归守卫: 未来 string 映射漂移 (例如 typo "Canceled" 或漏掉 case) → 测试拦截.
TEST_CASE("ErrorCode::Cancelled string round-trip",
          "[tool_result][cancel][realllm-followup]") {
  auto r = ToolResult::error(ErrorCode::Cancelled, "user cancelled");
  auto j = r.to_json();
  auto roundtripped = ToolResult::from_json(j);
  REQUIRE(roundtripped.ok == false);
  REQUIRE(roundtripped.error_code.has_value());
  REQUIRE(roundtripped.error_code.value() == ErrorCode::Cancelled);
  // meta.error_message 字段保留 ("user cancelled")
  REQUIRE(roundtripped.meta.value("error_message", "") == "user cancelled");
}