// tests/test_causal_ordering.cpp
// ADR-0037 causal-ordering-completion: T6 causal_order.h + T7 pure function tests
// Section A: causal_order 纯函数判定 (L2 → L1 → Concurrent 默认)
// Section B: 传递性 (调用方链式推导)
// Section C: ToolResult parent_trace 序列化 (T2 余量)
// Section D: 跨 Worker 端到端因果链集成测试 (T8)
// 设计依据: ADR-0037 §4.1 + OpenSpec change 2026-09-10-adr-0037-causal-ordering-completion
#include "catch_amalgamated.hpp"

#include "agenticdsl/contract/bus_event.h"
#include "agenticdsl/contract/causal_order.h"
#include "agenticdsl/contract/inmemory_bus.h"
#include "agenticdsl/cognitive/cognitive_worker.h"
#include "agenticdsl/cognitive/domain_worker_pool.h"
#include "agenticdsl/contract/i_llm_provider_decorator.h"
#include "core/engine.h"
#include "core/types/tool_result.h"
#include "common/llm/mock_provider.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

using agenticdsl::BusEvent;
using agenticdsl::DomainTask;
using agenticdsl::DomainWorkerPool;
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

// ============================================================================
// Section D: 跨 Worker 因果链端到端集成测试 (T8)
// ============================================================================

namespace {
// 等待条件谓词为 true, 超时 5s
template <typename Pred>
void wait_until_test(Pred&& pred,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    const auto start = std::chrono::steady_clock::now();
    while (!pred()) {
        if (std::chrono::steady_clock::now() - start > timeout) {
            FAIL("wait_until: timeout");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// 简单 mock DSL — start/end 占位
const std::string kEmptyDslForCausal = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";
}  // namespace

TEST_CASE("CognitiveWorker A→B 因果链 (parent_trace 透传至 payload, causal_order 判定)",
          "[causal_ordering][integration][cognitive_worker]") {
    auto bus = std::make_shared<agenticdsl::InMemoryBus>();
    auto engine = agenticdsl::DSLEngine::from_markdown(kEmptyDslForCausal);

    // 配置 mock LLM (返回最小合法响应)
    {
        auto* mock_from_inner =
            dynamic_cast<agenticdsl::MockLLMProvider*>(engine->get_llm_provider());
        if (!mock_from_inner) {
            if (auto* d = dynamic_cast<agenticdsl::ILLMProviderDecorator*>(engine->get_llm_provider())) {
                mock_from_inner =
                    dynamic_cast<agenticdsl::MockLLMProvider*>(d->inner());
            }
        }
        if (mock_from_inner) {
            mock_from_inner->set_fixed_response(R"({"tool":"none","args":{}})");
        }
    }

    // 订阅 cognitive.task.completed
    std::mutex events_mutex;
    std::vector<BusEvent> completed_events;
    bus->subscribe("cognitive.task.completed", [&](const BusEvent& e) {
        std::lock_guard<std::mutex> lock(events_mutex);
        completed_events.push_back(e);
    });

    agenticdsl::CognitiveWorker worker(std::move(engine), bus);
    worker.start();

    // 1) 提交 task-A (parent_trace = std::nullopt)
    worker.submit_task("task-A", "prompt-A");
    // 2) 提交 task-B (parent_trace = "task-A")
    worker.submit_task("task-B", "prompt-B", "task-A");

    // 等待 2 个 completed 事件
    wait_until_test([&] {
        std::lock_guard<std::mutex> lock(events_mutex);
        return completed_events.size() >= 2;
    });

    worker.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 验证: 捕获 evt_A (task-A 完成) + evt_B (task-B 完成, payload.parent_trace = "task-A")
    std::lock_guard<std::mutex> lock(events_mutex);
    REQUIRE(completed_events.size() == 2);

    const BusEvent& evt_a = completed_events[0];
    const BusEvent& evt_b = completed_events[1];

    REQUIRE(evt_a.payload.trace_id.has_value());
    REQUIRE(*evt_a.payload.trace_id == "task-A");
    REQUIRE(evt_a.payload.parent_trace == std::nullopt);

    REQUIRE(evt_b.payload.trace_id.has_value());
    REQUIRE(*evt_b.payload.trace_id == "task-B");
    REQUIRE(evt_b.payload.parent_trace.has_value());
    REQUIRE(*evt_b.payload.parent_trace == "task-A");

    // 因果判定: causal_order(evt_a, evt_b) 应返回 ABeforeB
    //   L2: evt_a.payload.trace_id == "task-A" == evt_b.payload.parent_trace → ABeforeB
    REQUIRE(causal_order(evt_a, evt_b) == CausalRelation::ABeforeB);
}

TEST_CASE("DomainWorkerPool A→B 因果链 (DomainTask.parent_trace 透传)",
          "[causal_ordering][integration][domain_worker_pool]") {
    auto bus = std::make_shared<agenticdsl::InMemoryBus>();
    DomainWorkerPool pool(4, bus);

    // 注册 echo handler
    pool.register_domain_handler("echo", [](const DomainTask& task) -> nlohmann::json {
        return nlohmann::json{{"echo", task.arguments}};
    });

    std::mutex events_mutex;
    std::vector<BusEvent> completed_events;
    bus->subscribe("domain.task.completed", [&](const BusEvent& e) {
        std::lock_guard<std::mutex> lock(events_mutex);
        completed_events.push_back(e);
    });

    pool.start();

    DomainTask task_a;
    task_a.domain = "echo";
    task_a.tool_name = "echo::test";
    task_a.arguments = nlohmann::json{{"msg", "A"}};
    task_a.output_key = "out_a";
    pool.submit_task(task_a);

    DomainTask task_b = task_a;
    task_b.arguments = nlohmann::json{{"msg", "B"}};
    task_b.output_key = "out_b";
    task_b.parent_trace = "domain-task-a";
    pool.submit_task(task_b);

    wait_until_test([&] {
        std::lock_guard<std::mutex> lock(events_mutex);
        return completed_events.size() >= 2;
    });
    pool.stop();

    std::lock_guard<std::mutex> lock(events_mutex);
    REQUIRE(completed_events.size() == 2);

    const BusEvent& evt_a = completed_events[0];
    const BusEvent& evt_b = completed_events[1];

    REQUIRE(evt_a.payload.parent_trace == std::nullopt);

    REQUIRE(evt_b.payload.parent_trace.has_value());
    REQUIRE(*evt_b.payload.parent_trace == "domain-task-a");

    REQUIRE(causal_order(evt_a, evt_b) == CausalRelation::ABeforeB);
}