// tests/test_command_compact.cpp
// chat-real-llm-coverage Phase A.2: /compact 命令路径覆盖
//
// 验证:
//   A.2.2 make_compact_command_spec 字段正确
//   A.2.3 handler 返回 placeholder "Compaction not yet wired (Task 8)"
//   A.2.4 handler 多次调用 idempotent (无副作用, 纯函数)

#include <catch_amalgamated.hpp>

#include "commands/compact_command.h"

TEST_CASE("/compact spec fields are correct",
          "[chat-real-llm-coverage][command]") {
  auto spec = pdk_chat_demo::make_compact_command_spec();
  REQUIRE(spec.name == "/compact");
  REQUIRE_FALSE(spec.description.empty());
  REQUIRE(spec.plugin_origin == "pdk_chat_demo");
  REQUIRE(spec.handler != nullptr);
}

TEST_CASE("/compact handler returns Task 8 placeholder",
          "[chat-real-llm-coverage][command]") {
  auto spec = pdk_chat_demo::make_compact_command_spec();
  agenticdsl::ToolCallContext ctx;
  std::string output = spec.handler(ctx);

  // Placeholder 消息含 Task 8 标记
  REQUIRE(output.find("Compaction not yet wired") != std::string::npos);
  REQUIRE(output.find("Task 8") != std::string::npos);
}

TEST_CASE("/compact handler is idempotent (pure function)",
          "[chat-real-llm-coverage][command]") {
  auto spec = pdk_chat_demo::make_compact_command_spec();
  agenticdsl::ToolCallContext ctx;

  std::string out1 = spec.handler(ctx);
  std::string out2 = spec.handler(ctx);
  std::string out3 = spec.handler(ctx);

  REQUIRE(out1 == out2);
  REQUIRE(out2 == out3);
}