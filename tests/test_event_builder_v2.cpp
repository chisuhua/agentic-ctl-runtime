// tests/test_event_builder_v2.cpp
// V2 EventBuilder 扩展测试 (promote-event-builder-fulltoolresult-support)
// 覆盖 full-payload constructor + 5 setters (ok/error_code/latency_ms/trace_id/metadata)
#include "catch_amalgamated.hpp"
#include "agenticdsl/contract/event_builder.h"
#include "agenticdsl/contract/bus_event.h"
#include "core/types/tool_result.h"

using namespace agenticdsl;

TEST_CASE("EventBuilder V2: full-payload constructor preserves all 7 fields",
          "[contract][event][v2]") {
  ToolResult tr;
  tr.ok = true;
  tr.data = {{"output_key", "value"}};
  tr.meta = {{"trace_id", "tid-1"}};
  tr.error_code = ErrorCode::Unknown;
  tr.latency_ms = 150;
  tr.trace_id = "trace-xyz";
  tr.metadata = nlohmann::json::object();
  tr.metadata.value()["cost"] = 0.05;

  auto ev = EventBuilder("tool.completed", tr).build();

  REQUIRE(ev.topic == "tool.completed");
  REQUIRE(ev.payload.ok == true);
  REQUIRE(ev.payload.data["output_key"] == "value");
  REQUIRE(ev.payload.meta["trace_id"] == "tid-1");
  REQUIRE(ev.payload.latency_ms.has_value());
  REQUIRE(ev.payload.latency_ms.value() == 150);
  REQUIRE(ev.payload.trace_id.has_value());
  REQUIRE(ev.payload.trace_id.value() == "trace-xyz");
  REQUIRE(ev.payload.metadata.has_value());
  REQUIRE(ev.payload.metadata.value()["cost"] == 0.05);
}

TEST_CASE("EventBuilder V2: full-payload constructor preserves failure semantics",
          "[contract][event][v2]") {
  ToolResult tr = ToolResult::error(ErrorCode::PermissionDenied,
                                    "Layer denied",
                                    {{"caller_layer", "cognitive"}});

  auto ev = EventBuilder("tool.audit.denied", tr).build();

  REQUIRE(ev.topic == "tool.audit.denied");
  REQUIRE(ev.payload.ok == false);
  REQUIRE(ev.payload.error_code.has_value());
  REQUIRE(ev.payload.error_code.value() == ErrorCode::PermissionDenied);
  REQUIRE(ev.payload.meta["caller_layer"] == "cognitive");
}

TEST_CASE("EventBuilder V2: .ok(false) override sets payload.ok to false",
          "[contract][event][v2]") {
  auto ev = EventBuilder("test.topic").ok(false).build();
  REQUIRE(ev.payload.ok == false);
}

TEST_CASE("EventBuilder V2: .ok(true) returns to default true",
          "[contract][event][v2]") {
  auto ev = EventBuilder("test.topic").ok(true).build();
  REQUIRE(ev.payload.ok == true);
}

TEST_CASE("EventBuilder V2: .error_code setter sets payload.error_code",
          "[contract][event][v2]") {
  auto ev = EventBuilder("tool.failed")
                .error_code(ErrorCode::ResourceExhausted)
                .build();
  REQUIRE(ev.payload.error_code.has_value());
  REQUIRE(ev.payload.error_code.value() == ErrorCode::ResourceExhausted);
}

TEST_CASE("EventBuilder V2: .latency_ms setter sets payload.latency_ms",
          "[contract][event][v2]") {
  auto ev = EventBuilder("llm.response").latency_ms(250).build();
  REQUIRE(ev.payload.latency_ms.has_value());
  REQUIRE(ev.payload.latency_ms.value() == 250);
}

TEST_CASE("EventBuilder V2: .trace_id setter sets payload.trace_id",
          "[contract][event][v2]") {
  auto ev = EventBuilder("tool.execution.start").trace_id("abc-123").build();
  REQUIRE(ev.payload.trace_id.has_value());
  REQUIRE(ev.payload.trace_id.value() == "abc-123");
}

TEST_CASE("EventBuilder V2: .parent_trace setter sets payload.parent_trace (ADR-0037 L2)",
          "[contract][event][v2][causal_ordering]") {
  auto ev = EventBuilder("task.started").parent_trace("task-P-1").build();
  REQUIRE(ev.payload.parent_trace.has_value());
  REQUIRE(ev.payload.parent_trace.value() == "task-P-1");
}

TEST_CASE("EventBuilder V2: .metadata setter sets payload.metadata distinct from meta",
          "[contract][event][v2]") {
  auto ev = EventBuilder("x")
                .meta({{"session_id", "sess-1"}})
                .metadata({{"custom", "value"}})
                .build();

  REQUIRE(ev.payload.meta["session_id"] == "sess-1");
  REQUIRE(ev.payload.metadata.has_value());
  REQUIRE(ev.payload.metadata.value()["custom"] == "value");
  // meta 与 metadata 互不污染
  REQUIRE(ev.payload.meta.contains("custom") == false);
}

TEST_CASE("EventBuilder V2: chained setters compose correctly",
          "[contract][event][v2]") {
  auto ev = EventBuilder("tool.execution.end")
                .args({{"tool", "shell.exec"}})
                .meta({{"trace_id", "tid-42"}})
                .ok(true)
                .latency_ms(100)
                .error_code(ErrorCode::Unknown)
                .trace_id("full-trace")
                .build();

  REQUIRE(ev.topic == "tool.execution.end");
  REQUIRE(ev.payload.ok == true);
  REQUIRE(ev.payload.data["tool"] == "shell.exec");
  REQUIRE(ev.payload.meta["trace_id"] == "tid-42");
  REQUIRE(ev.payload.latency_ms.value() == 100);
  REQUIRE(ev.payload.error_code.value() == ErrorCode::Unknown);
  REQUIRE(ev.payload.trace_id.value() == "full-trace");
}
