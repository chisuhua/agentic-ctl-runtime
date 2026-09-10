// tests/test_causal_ordering.cpp
// ADR-0037 causal-ordering-completion: T6 causal_order.h + T7 pure function tests
// Section A: causal_order 纯函数判定 (L2 → L1 → Concurrent 默认)
// Section B: 传递性 (调用方链式推导)
// 设计依据: ADR-0037 §4.1 + OpenSpec change 2026-09-10-adr-0037-causal-ordering-completion
#include "catch_amalgamated.hpp"

#include "agenticdsl/contract/bus_event.h"
#include "agenticdsl/contract/causal_order.h"

#include <chrono>

using agenticdsl::BusEvent;
using agenticdsl::ToolResult;
using agenticdsl::event::CausalRelation;
using agenticdsl::event::causal_order;
using agenticdsl::event::happens_before;

namespace {

// Helper: 构造测试用 BusEvent (topic + payload + causal_time)
BusEvent make_event(const std::string& topic, uint64_t causal_time,
                    std::optional<std::string> trace_id = std::nullopt,
                    std::optional<std::string> parent_trace = std::nullopt) {
    BusEvent e;
    e.topic = topic;
    e.causal_time = causal_time;
    e.payload.trace_id = std::move(trace_id);
    e.payload.parent_trace = std::move(parent_trace);
    return e;
}

}  // namespace

// ============================================================================
// Section A: causal_order() 纯函数判定
// ============================================================================

TEST_CASE("causal_order: L2 显式因果链匹配 (payload.trace_id == payload.parent_trace)",
          "[causal_ordering][causal_order]") {
    // WHEN a.payload.trace_id == "task-A-123"
    // AND b.payload.parent_trace == "task-A-123"
    auto a = make_event("task.completed", 10, "task-A-123");
    auto b = make_event("task.started", 20, std::nullopt, "task-A-123");

    REQUIRE(causal_order(a, b) == CausalRelation::ABeforeB);
    REQUIRE(happens_before(a, b) == true);
    REQUIRE(happens_before(b, a) == false);
}

TEST_CASE("causal_order: L1 causal_time 回退 (a.causal_time < b.causal_time)",
          "[causal_ordering][causal_order]") {
    // WHEN 无 L2 匹配 (parent_trace 缺值)
    // AND a.causal_time=10 < b.causal_time=20
    auto a = make_event("evt.a", 10, "x");
    auto b = make_event("evt.b", 20, "y");

    REQUIRE(causal_order(a, b) == CausalRelation::ABeforeB);
}

TEST_CASE("causal_order: L1 causal_time 反向 (a.causal_time > b.causal_time)",
          "[causal_ordering][causal_order]") {
    auto a = make_event("evt.a", 20, "x");
    auto b = make_event("evt.b", 10, "y");

    REQUIRE(causal_order(a, b) == CausalRelation::BBeforeA);
}

TEST_CASE("causal_order: causal_time == 0 sentinel → Concurrent (不触发 L1 回退)",
          "[causal_ordering][causal_order]") {
    // WHEN 两者 causal_time 都为 0 (BusEvent 字段默认 sentinel)
    // AND 无 L2 匹配
    auto a = make_event("evt.a", 0, "x");
    auto b = make_event("evt.b", 0, "y");

    REQUIRE(causal_order(a, b) == CausalRelation::Concurrent);
}

// ============================================================================
// Section B: 传递性 (调用方链式推导)
// ============================================================================

TEST_CASE("causal_order: A→B→C 链式因果 (传递性由调用方链式推导)",
          "[causal_ordering][transitivity]") {
    // 构造 3 跳链: a.payload.trace_id=A, b.payload.parent_trace=A, b.payload.trace_id=B, c.payload.parent_trace=B
    auto a = make_event("task.a.completed", 100, "A");
    auto b = make_event("task.b.started",  200, "B", "A");
    auto c = make_event("task.c.started",  300, "C", "B");

    // 直接 1-hop 判定
    REQUIRE(causal_order(a, b) == CausalRelation::ABeforeB);
    REQUIRE(causal_order(b, c) == CausalRelation::ABeforeB);

    // 传递性: a 与 c 无 L2 匹配 (c.payload.parent_trace="B" != a.payload.trace_id="A")
    // 调用方必须手动链式推导:
    //   causal_order(a, b) == ABeforeB AND causal_order(b, c) == ABeforeB
    //   => a happens-before c
    // 函数本身仅判 1-hop (ADR-0037 line 538)
    // 因此 causal_order(a, c) 在无 L2 匹配时回退到 L1: a.causal_time=100 < c.causal_time=300
    REQUIRE(causal_order(a, c) == CausalRelation::ABeforeB);  // 由 L1 回退 + 实际单向链
}

// ============================================================================
// Section C: ToolResult parent_trace 序列化 (T2 余量)
// ============================================================================

TEST_CASE("ToolResult parent_trace: 字段存在 round-trip (to_json → from_json)",
          "[causal_ordering][serialization]") {
    ToolResult t;
    t.parent_trace = "task-A-123";

    auto j = t.to_json();
    REQUIRE(j.contains("parent_trace"));
    REQUIRE(j["parent_trace"] == "task-A-123");

    auto r = ToolResult::from_json(j);
    REQUIRE(r.parent_trace.has_value());
    REQUIRE(*r.parent_trace == "task-A-123");
}

TEST_CASE("ToolResult parent_trace: 缺值容错 (旧 JSON 无 parent_trace → nullopt, 不抛异常)",
          "[causal_ordering][serialization]") {
    // 模拟 ship 前持久化的旧 JSONL 数据 (无 parent_trace 字段)
    nlohmann::json legacy = {
        {"ok", true},
        {"data", {{"output", "legacy"}}},
        {"meta", {}},
        {"trace_id", "task-old-1"}
    };

    auto r = ToolResult::from_json(legacy);
    REQUIRE(r.ok == true);
    REQUIRE(r.parent_trace == std::nullopt);  // 缺值容错
    REQUIRE(r.trace_id.has_value());           // 其他字段正常
    REQUIRE(*r.trace_id == "task-old-1");
}