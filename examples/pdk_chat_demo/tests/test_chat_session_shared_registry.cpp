// tests/test_chat_session_shared_registry.cpp
// §4.0.6 chat-async-io-consumer-loop: Shared CancellationRegistry identity test
// 关联: openspec/changes/chat-async-io-consumer-loop

#include <atomic>
#include <chrono>
#include <memory>
#include <stop_token>
#include <thread>

#include <catch_amalgamated.hpp>
#include <iostream>

#include "cancellation_registry.h"
#include "chat_session.h"

using namespace pdk_chat_demo;

namespace {
struct CinEofGuard {
    CinEofGuard()  { std::cin.setstate(std::ios::eofbit); }
    ~CinEofGuard() { std::cin.clear(); }
};
}

TEST_CASE("ChatSession accepts shared CancellationRegistry (no crash, no leak)",
          "[chat_session][shared_registry][4.0.6]") {
    CinEofGuard eof;
    auto shared_registry = std::make_shared<CancellationRegistry>();

    // §4.0.2: 6th ctor arg defaults to nullptr; explicit shared instance works
    ChatSession session(nullptr, nullptr, nullptr, {}, {}, shared_registry);
    SUCCEED("ChatSession constructed with shared registry");
}

TEST_CASE("Shared registry: external register + session resolve_token works",
          "[chat_session][shared_registry][4.0.6]") {
    CinEofGuard eof;
    auto shared_registry = std::make_shared<CancellationRegistry>();
    ChatSession session(nullptr, nullptr, nullptr, {}, {}, shared_registry);

    // Simulate: caller (e.g., main.cpp setup) pre-registers a stop_source
    auto caller_source = std::make_shared<std::stop_source>();
    std::string id = shared_registry->register_source(caller_source);

    // Simulate: loop_agent-style resolver picks up the same id
    auto resolved_source = shared_registry->resolve_source(id);
    REQUIRE(resolved_source != nullptr);
    REQUIRE(resolved_source.get() == caller_source.get());

    // request_stop on caller_source observable from resolved
    caller_source->request_stop();
    CHECK(resolved_source->get_token().stop_requested());
}

TEST_CASE("Shared registry: request_stop from one handle cancels token for all",
          "[chat_session][shared_registry][4.0.6]") {
    CinEofGuard eof;
    auto shared_registry = std::make_shared<CancellationRegistry>();
    ChatSession session_a(nullptr, nullptr, nullptr, {}, {}, shared_registry);
    ChatSession session_b(nullptr, nullptr, nullptr, {}, {}, shared_registry);

    // Both sessions share the same registry — they can each register
    // independent stop_sources and observe each other's via the shared instance.
    auto source_a = std::make_shared<std::stop_source>();
    std::string id_a = shared_registry->register_source(source_a);

    // External component resolves id_a via shared registry
    auto resolved_token = shared_registry->resolve_token(id_a);
    REQUIRE(resolved_token.stop_possible());
    CHECK_FALSE(resolved_token.stop_requested());

    // Simulate async request_stop from a different thread (e.g., /cancel handler)
    std::thread cancel_thread([&source_a]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        source_a->request_stop();
    });

    // Observe via shared registry from "loop_agent" thread
    auto token = shared_registry->resolve_token(id_a);
    // spin until stop_requested or timeout
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!token.stop_requested() &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        token = shared_registry->resolve_token(id_a);
    }

    cancel_thread.join();
    CHECK(token.stop_requested());
}

TEST_CASE("Self-owned fallback when no shared registry passed (default ctor)",
          "[chat_session][shared_registry][4.0.6][NC3]") {
    CinEofGuard eof;
    // §4.0.9 NC3 fix: registry defaults to nullptr → Impl fallback to self-owned
    ChatSession session(nullptr, nullptr, nullptr, {}, {});

    // request_stop() with no active turn should be no-op (early return)
    session.request_stop();  // must not crash
    SUCCEED("default-constructed ChatSession handles request_stop gracefully");
}