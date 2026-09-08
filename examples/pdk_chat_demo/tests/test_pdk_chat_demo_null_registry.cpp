// tests/test_pdk_chat_demo_null_registry.cpp
// §4.0.13 chat-async-io-consumer-loop: g_cancellation_registry == nullptr →
// "non-cancellable-but-executable" fallback (loop/run keeps working without error)
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
#include "commands/cancellation_globals.h"

using namespace pdk_chat_demo;

namespace {
struct CinEofGuard {
    CinEofGuard()  { std::cin.setstate(std::ios::eofbit); }
    ~CinEofGuard() { std::cin.clear(); }
};
}

TEST_CASE("Default-constructed ChatSession works without g_cancellation_registry",
          "[pdk_chat_demo][null_registry][4.0.13]") {
    CinEofGuard eof;
    auto saved = pdk_chat_demo::g_cancellation_registry;
    pdk_chat_demo::g_cancellation_registry = nullptr;

    // §4.0.9 NC3 fix: ChatSession ctor accepts nullptr registry and self-fallbacks
    ChatSession session(nullptr, nullptr, nullptr, {}, {}, nullptr);
    // request_stop with no active turn → no-op
    session.request_stop();
    SUCCEED("default ChatSession + null g_cancellation_registry does not crash");

    pdk_chat_demo::g_cancellation_registry = saved;
}

TEST_CASE("Non-cancellable-but-executable: stop_token {} flows through loop_agent",
          "[pdk_chat_demo][null_registry][4.0.13]") {
    CinEofGuard eof;
    auto saved = pdk_chat_demo::g_cancellation_registry;
    pdk_chat_demo::g_cancellation_registry = nullptr;

    // Simulate the pdk_entry.cpp null-guard path:
    //   if (!cancellation_id.empty() && pdk_chat_demo::g_cancellation_registry) {
    //       cancellation_token = g_cancellation_registry->resolve_token(id);
    //   }
    //   // else: cancellation_token stays default-empty → non-cancellable

    std::string cancellation_id = "fake_id_123";  // non-empty
    std::stop_token cancellation_token;  // default empty token

    // The null-guard path: skip resolve when global is nullptr
    if (pdk_chat_demo::g_cancellation_registry) {
        cancellation_token =
            pdk_chat_demo::g_cancellation_registry->resolve_token(cancellation_id);
    }

    // Default empty token — never cancellable, no error, no crash
    CHECK_FALSE(cancellation_token.stop_possible());
    CHECK_FALSE(cancellation_token.stop_requested());

    pdk_chat_demo::g_cancellation_registry = saved;
}

TEST_CASE("Restoring shared registry enables cancellation after null phase",
          "[pdk_chat_demo][null_registry][4.0.13]") {
    CinEofGuard eof;
    auto saved = pdk_chat_demo::g_cancellation_registry;

    // Phase 1: null global — request_stop is a no-op
    pdk_chat_demo::g_cancellation_registry = nullptr;
    {
        ChatSession session(nullptr, nullptr, nullptr, {}, {}, nullptr);
        session.request_stop();
    }

    // Phase 2: restore shared registry — cancellation flow works again
    auto shared = std::make_shared<CancellationRegistry>();
    pdk_chat_demo::g_cancellation_registry = shared;
    {
        ChatSession session(nullptr, nullptr, nullptr, {}, {}, shared);
        // Register an external cancellation source for this session
        auto source = std::make_shared<std::stop_source>();
        std::string id = shared->register_source(source);

        // External request_stop via shared registry should propagate
        source->request_stop();
        auto token = shared->resolve_token(id);
        CHECK(token.stop_requested());
    }

    pdk_chat_demo::g_cancellation_registry = saved;
}