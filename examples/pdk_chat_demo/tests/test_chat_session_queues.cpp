// tests/test_chat_session_queues.cpp
// ChatSession queue infrastructure tests (Phase A - steering + follow-up queues)
// 关联: openspec/changes/chat-async-io-queue-infra

#include "catch_amalgamated.hpp"
#include "chat_session.h"
#include <iostream>

using namespace pdk_chat_demo;

TEST_CASE("queue_size reflects enqueue count", "[chat_session][queue]") {
  ChatSession session(nullptr, nullptr, nullptr, {}, {});

  SECTION("steering") {
    session.try_push_steering_for_test("/model openai");
    session.try_push_steering_for_test("/cancel");
    REQUIRE(session.queue_size(QueueKind::Steering) == 2);
  }
  SECTION("follow_up") {
    session.try_push_follow_up_for_test("hello world");
    session.try_push_follow_up_for_test("another");
    REQUIRE(session.queue_size(QueueKind::FollowUp) == 2);
  }
}

TEST_CASE("steering_queue overflow rejects at capacity", "[chat_session][queue][overflow]") {
  ChatSession session(nullptr, nullptr, nullptr, {}, {});

  for (int i = 0; i < 32; ++i) {
    REQUIRE(session.try_push_steering_for_test("/cmd_" + std::to_string(i)));
  }
  REQUIRE_FALSE(session.try_push_steering_for_test("/overflow"));
  REQUIRE(session.queue_size(QueueKind::Steering) == 32);
}

TEST_CASE("try_clear_queue returns count cleared", "[chat_session][queue][clear]") {
  ChatSession session(nullptr, nullptr, nullptr, {}, {});

  session.try_push_steering_for_test("/a");
  session.try_push_steering_for_test("/b");
  session.try_push_follow_up_for_test("x");

  REQUIRE(session.try_clear_queue(QueueKind::Steering) == 2);
  REQUIRE(session.queue_size(QueueKind::Steering) == 0);
  REQUIRE(session.try_clear_queue(QueueKind::FollowUp) == 1);
  REQUIRE(session.queue_size(QueueKind::FollowUp) == 0);
}

TEST_CASE("input thread joins on destruction", "[chat_session][queue][thread]") {
  // Set EOF on cin to unblock input thread before destruction
  std::cin.setstate(std::ios::eofbit);
  {
    // ⚠️ 显式开启 input thread (默认已改 false, 见 chat_session.h):
    // 本 case 专门验证 input_thread_main 在 EOF 下正常退出 + join.
    SessionConfig cfg;
    cfg.enable_input_thread = true;
    ChatSession session(nullptr, nullptr, nullptr, {}, cfg);
  }
  SUCCEED("input thread joined without hanging");
}
