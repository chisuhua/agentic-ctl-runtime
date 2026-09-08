// tests/test_chat_session_consumer.cpp
// ChatSession consumer loop tests (Phase §8 chat-async-io-consumer-loop)
// 关联: openspec/changes/chat-async-io-consumer-loop

#include "catch_amalgamated.hpp"
#include "chat_session.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

using namespace pdk_chat_demo;
using namespace std::chrono_literals;

namespace {

// Set EOF on cin to unblock input thread (avoids hangs during destruction)
struct CinEofGuard {
    CinEofGuard()  { std::cin.setstate(std::ios::eofbit); }
    ~CinEofGuard() {
        std::cin.clear();
    }
};

}  // namespace

TEST_CASE("try_pop_input priority: steering before follow-up", "[chat_session][consumer][priority]") {
    CinEofGuard eof;
    ChatSession session(nullptr, nullptr, nullptr, {}, {});

    // Push one of each — steering should win regardless of order
    session.try_push_follow_up_for_test("follow-up msg");
    session.try_push_steering_for_test("/steering msg");

    auto msg = session.try_pop_input();
    REQUIRE(msg.has_value());
    REQUIRE(msg->kind == QueueKind::Steering);
    REQUIRE(msg->text == "/steering msg");

    // follow-up remains
    auto msg2 = session.try_pop_input();
    REQUIRE(msg2.has_value());
    REQUIRE(msg2->kind == QueueKind::FollowUp);
    REQUIRE(msg2->text == "follow-up msg");
}

TEST_CASE("try_pop_input returns nullopt when empty", "[chat_session][consumer][empty]") {
    CinEofGuard eof;
    ChatSession session(nullptr, nullptr, nullptr, {}, {});
    REQUIRE_FALSE(session.try_pop_input().has_value());
}

TEST_CASE("pop_next_input returns promptly when input thread shutdown (EOF)",
          "[chat_session][consumer][blocking]") {
    CinEofGuard eof;
    ChatSession session(nullptr, nullptr, nullptr, {}, {});

    // Test env: stdin already EOF → input thread sets stop_input_thread_
    // immediately → pop_next_input returns nullopt. Real blocking is exercised
    // by Phase 9 E2E tests (test_pdk_chat_demo_stdin_e2e) with live stdin pipe.
    auto start = std::chrono::steady_clock::now();
    auto msg = session.pop_next_input(2000ms);
    auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE_FALSE(msg.has_value());
    REQUIRE(elapsed < 100ms);
}

TEST_CASE("§7.4 regression: timeout returns nullopt WITHOUT shutdown (no input thread)",
          "[chat_session][consumer][regression][7.4]") {
    // Regression guard for the main-loop bug fixed in commit a759db6.
    // enable_input_thread = false → no input thread → stop_input_thread_ stays false.
    // pop_next_input(timeout) on empty queue must return nullopt WITHOUT shutdown signal,
    // so the main loop's `if (is_input_thread_shutdown()) break` won't fire.
    SessionConfig session_cfg;
    session_cfg.enable_input_thread = false;
    ChatSession session(nullptr, nullptr, nullptr, {}, session_cfg);

    REQUIRE_FALSE(session.is_input_thread_shutdown());

    // Empty: no message, no shutdown
    auto start = std::chrono::steady_clock::now();
    auto msg1 = session.pop_next_input(50ms);
    auto elapsed1 = std::chrono::steady_clock::now() - start;
    REQUIRE_FALSE(msg1.has_value());
    REQUIRE(elapsed1 >= 50ms);  // waited full timeout
    REQUIRE_FALSE(session.is_input_thread_shutdown());  // CRITICAL: still alive

    // Push + pop proves session is alive
    session.try_push_follow_up_for_test("alive-check");
    auto msg2 = session.pop_next_input(50ms);
    REQUIRE(msg2.has_value());
    REQUIRE(msg2->text == "alive-check");
    REQUIRE_FALSE(session.is_input_thread_shutdown());
}

TEST_CASE("multi-round interaction: 5 messages processed sequentially, session stays alive",
          "[chat_session][consumer][multi_round]") {
    SessionConfig session_cfg;
    session_cfg.enable_input_thread = false;
    ChatSession session(nullptr, nullptr, nullptr, {}, session_cfg);

    const std::vector<std::string> inputs = {
        "hi", "what can you do?", "tell me a joke", "thanks", "exit",
    };

    for (const auto& input : inputs) {
        session.try_push_follow_up_for_test(input);
        auto msg = session.pop_next_input(100ms);
        REQUIRE(msg.has_value());
        REQUIRE(msg->kind == QueueKind::FollowUp);
        REQUIRE(msg->text == input);
        REQUIRE_FALSE(session.is_input_thread_shutdown());  // still alive after each round
    }

    // After draining all: next pop is timeout, NOT shutdown
    auto empty = session.pop_next_input(50ms);
    REQUIRE_FALSE(empty.has_value());
    REQUIRE_FALSE(session.is_input_thread_shutdown());

    // Multi-round steering interleaved with follow-up (priority order: steering first)
    session.try_push_follow_up_for_test("follow-up-1");
    session.try_push_steering_for_test("/cancel");
    session.try_push_follow_up_for_test("follow-up-2");

    auto m1 = session.try_pop_input();
    REQUIRE(m1.has_value());
    REQUIRE(m1->kind == QueueKind::Steering);  // steering priority
    REQUIRE(m1->text == "/cancel");

    auto m2 = session.try_pop_input();
    REQUIRE(m2.has_value());
    REQUIRE(m2->kind == QueueKind::FollowUp);
    REQUIRE(m2->text == "follow-up-1");

    auto m3 = session.try_pop_input();
    REQUIRE(m3.has_value());
    REQUIRE(m3->kind == QueueKind::FollowUp);
    REQUIRE(m3->text == "follow-up-2");

    REQUIRE_FALSE(session.is_input_thread_shutdown());
}

TEST_CASE("pop_next_input timeout returns nullopt", "[chat_session][consumer][timeout]") {
    CinEofGuard eof;
    ChatSession session(nullptr, nullptr, nullptr, {}, {});

    auto start = std::chrono::steady_clock::now();
    auto msg = session.pop_next_input(50ms);
    auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE_FALSE(msg.has_value());
    // CinEofGuard triggers stop_input_thread_ via EOF — predicate returns true
    // immediately, so elapsed is near 0 (not >= 50ms). Verify it's still bounded.
    REQUIRE(elapsed < 100ms);
}

TEST_CASE("try_peek_input does not consume", "[chat_session][consumer][peek]") {
    CinEofGuard eof;
    ChatSession session(nullptr, nullptr, nullptr, {}, {});

    session.try_push_steering_for_test("/peek-target");
    session.try_push_follow_up_for_test("follow-up");

    // First peek returns steering (priority order)
    auto peeked1 = session.try_peek_input();
    REQUIRE(peeked1.has_value());
    REQUIRE(peeked1->kind == QueueKind::Steering);
    REQUIRE(peeked1->text == "/peek-target");

    // Second peek returns SAME steering (not consumed)
    auto peeked2 = session.try_peek_input();
    REQUIRE(peeked2.has_value());
    REQUIRE(peeked2->kind == QueueKind::Steering);
    REQUIRE(peeked2->text == "/peek-target");

    // Queue size unchanged — both still present
    REQUIRE(session.queue_size(QueueKind::Steering) == 1);
    REQUIRE(session.queue_size(QueueKind::FollowUp) == 1);

    // Now pop drains steering, peek falls through to follow-up
    auto popped = session.try_pop_input();
    REQUIRE(popped.has_value());
    REQUIRE(popped->kind == QueueKind::Steering);

    auto peeked3 = session.try_peek_input();
    REQUIRE(peeked3.has_value());
    REQUIRE(peeked3->kind == QueueKind::FollowUp);
}

TEST_CASE("concurrent push + try_pop: pending_input_count_ invariant holds",
          "[chat_session][consumer][stress][oracle-c2]") {
    CinEofGuard eof;
    ChatSession session(nullptr, nullptr, nullptr, {}, {});

    // Capacity is 32/queue (kDefaultQueueCapacity), total cap = 64.
    // Use bounded workload to verify pushed == consumed invariant
    // (no message lost under concurrent producer stress).
    constexpr int kThreads = 4;
    constexpr int kPerThread = 16;  // 4 * 16 = 64 total (fits in 2*32 capacity)
    std::atomic<int> pushed{0};

    // Phase 1: concurrent producers
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&session, t, &pushed]() {
            for (int i = 0; i < kPerThread; ++i) {
                bool ok = (i % 2 == 0)
                  ? session.try_push_steering_for_test("/s_" + std::to_string(t) + "_" + std::to_string(i))
                  : session.try_push_follow_up_for_test("f_" + std::to_string(t) + "_" + std::to_string(i));
                if (ok) pushed.fetch_add(1);
            }
        });
    }
    for (auto& p : producers) p.join();

    REQUIRE(pushed.load() > 0);

    // Phase 2: sequential drain — no race between consumer and producer
    int consumed = 0;
    while (auto msg = session.try_pop_input()) {
        consumed++;
    }

    // Invariant: all pushed messages consumed, queues empty
    REQUIRE(consumed == pushed.load());
    REQUIRE(session.queue_size(QueueKind::Steering) == 0);
    REQUIRE(session.queue_size(QueueKind::FollowUp) == 0);
    REQUIRE_FALSE(session.try_pop_input().has_value());
}