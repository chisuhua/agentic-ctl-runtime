// tests/test_event_bus_soak.cpp
// BusEvent 类型验证 + InMemoryBus soak test (Change A)
#include "catch_amalgamated.hpp"
#include "agenticdsl/contract/bus_event.h"
#include "agenticdsl/contract/inmemory_bus.h"

#include <atomic>
#include <thread>
#include <vector>

TEST_CASE("BusEvent default construction", "[busevent]") {
    agenticdsl::BusEvent e;
    REQUIRE(e.topic.empty());
    REQUIRE(e.causal_time == 0);
    REQUIRE(e.priority == agenticdsl::EventPriority::Normal);
}

TEST_CASE("BusEvent construction with fields", "[busevent]") {
    auto now = std::chrono::steady_clock::now();
    agenticdsl::ToolResult tr;
    agenticdsl::BusEvent e{"test_topic", tr, now, 42, agenticdsl::EventPriority::Critical};
    REQUIRE(e.topic == "test_topic");
    REQUIRE(e.timestamp == now);
    REQUIRE(e.causal_time == 42);
    REQUIRE(e.priority == agenticdsl::EventPriority::Critical);
}

TEST_CASE("InMemoryBus soak — 10000 events no loss", "[busevent][soak]") {
    agenticdsl::InMemoryBus bus;
    std::atomic<size_t> received{0};
    constexpr size_t N = 10000;

    bus.subscribe("soak", [&](const agenticdsl::BusEvent&) {
        received.fetch_add(1, std::memory_order_relaxed);
    });

    std::vector<std::thread> producers;
    for (int i = 0; i < 4; ++i) {
        producers.emplace_back([&bus, N]() {
            for (size_t j = 0; j < N / 4; ++j) {
                bus.emit("soak", std::to_string(j));
            }
        });
    }
    for (auto& t : producers) t.join();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    bus.wait_for_drain();
    REQUIRE(received.load() == N);
}

// try_pop 契约: 空队列返回 false (emit + try_pop 与 dispatch_loop 线程竞争, 改用 subscribe 路径取件)
TEST_CASE("InMemoryBus try_pop returns false on empty queue", "[busevent]") {
    agenticdsl::InMemoryBus bus;
    agenticdsl::BusEvent out;
    REQUIRE_FALSE(bus.try_pop(out));
}

TEST_CASE("InMemoryBus subscribe receives emitted event", "[busevent]") {
    agenticdsl::InMemoryBus bus;
    std::atomic<int> count{0};
    std::string captured_topic;
    bus.subscribe("pop_test", [&](const agenticdsl::BusEvent& e) {
        ++count;
        captured_topic = e.topic;
    });
    agenticdsl::ToolResult tr;
    bus.emit(agenticdsl::BusEvent{"pop_test", tr});
    bus.wait_for_drain();
    REQUIRE(count.load() == 1);
    REQUIRE(captured_topic == "pop_test");
}