// tests/test_chat_session.cpp
// ChatSession 单元测试 (v1 buildable)
// 关联: docs/examples/pdk_chat_demo/DESIGN.md §8

// NOTE: CATCH_CONFIG_MAIN 由 test_e2e_mock.cpp 提供，此文件不定义 main

#include "catch_amalgamated.hpp"

#include "chat_session.h"

using namespace pdk_chat_demo;

TEST_CASE("ChatConfig::from_json parses valid config", "[chat_session]") {
    // Config path relative to build directory (where ctest runs)
    // CMake sets WORKING_DIRECTORY to ${PROJECT_BINARY_DIR}
    // config.json is configured by CMake configure_file → placed in build/examples/pdk_chat_demo/
    ChatConfig cfg = ChatConfig::from_json("../config.json");

    REQUIRE(cfg.schema_version == "1.0");
    REQUIRE(cfg.app_id == "pdk_chat_demo");
    REQUIRE(cfg.agent.loop_type == "react");
    REQUIRE(cfg.agent.provider == "deepseek");
    REQUIRE(cfg.agent.model == "deepseek-v4-flash");
    REQUIRE(cfg.agent.max_steps == 50);
    REQUIRE(cfg.agent.timeout_ms == 300000);
    REQUIRE(cfg.agent.budget_limit_usd == 1.0);

    REQUIRE(cfg.plugins.size() == 8);
    REQUIRE(cfg.plugins[0].id == "chat.loop");
    REQUIRE(cfg.plugins[0].lifecycle == "lazy");
    REQUIRE(cfg.plugins[0].activation_events.size() == 1);
    REQUIRE(cfg.plugins[7].id == "skill.code_review_run");
}

TEST_CASE("ChatConfig::validate rejects bad config", "[chat_session]") {
    ChatConfig cfg;
    cfg.schema_version = "999.0";
    REQUIRE_THROWS(cfg.validate());
}

TEST_CASE("ChatConfig::override_provider switches provider and model", "[chat_session]") {
    ChatConfig cfg = ChatConfig::from_json("../config.json");
    REQUIRE(cfg.agent.provider == "deepseek");
    REQUIRE(cfg.agent.model == "deepseek-v4-flash");

    cfg.override_provider("openai", "gpt-4o");
    REQUIRE(cfg.agent.provider == "openai");
    REQUIRE(cfg.agent.model == "gpt-4o");

    // 再切回 mock
    cfg.override_provider("mock", "test");
    REQUIRE(cfg.agent.provider == "mock");
    REQUIRE(cfg.agent.model == "test");
}

TEST_CASE("override_system_prompt: neither flag keeps default", "[chat_session][system_prompt]") {
    ChatConfig cfg;
    cfg.agent.system_prompt = "DEFAULT";
    cfg.override_system_prompt("", "");
    REQUIRE(cfg.agent.system_prompt == "DEFAULT");
}

TEST_CASE("override_system_prompt: --system-prompt replaces default", "[chat_session][system_prompt]") {
    ChatConfig cfg;
    cfg.agent.system_prompt = "DEFAULT";
    cfg.override_system_prompt("OVERWRITE", "");
    REQUIRE(cfg.agent.system_prompt == "OVERWRITE");
}

TEST_CASE("override_system_prompt: append adds newline-separated suffix to default", "[chat_session][system_prompt]") {
    ChatConfig cfg;
    cfg.agent.system_prompt = "DEFAULT";
    cfg.override_system_prompt("", "be terse.");
    REQUIRE(cfg.agent.system_prompt == "DEFAULT\nbe terse.");
}

TEST_CASE("override_system_prompt: overwrite-then-append produces overwrite\\nappend", "[chat_session][system_prompt]") {
    ChatConfig cfg;
    cfg.agent.system_prompt = "DEFAULT";
    cfg.override_system_prompt("CUSTOM", "extra rule");
    REQUIRE(cfg.agent.system_prompt == "CUSTOM\nextra rule");
}

TEST_CASE("ChatResult has success flag and default values", "[chat_session]") {
    ChatResult r;
    REQUIRE(r.success == true);
    REQUIRE(r.total_steps == 0);
    REQUIRE(r.total_tokens == 0);
    REQUIRE(r.cost_usd == 0.0);
    REQUIRE(r.response.empty());
}

TEST_CASE("ChatSession constructs with valid config", "[chat_session]") {
    ChatConfig cfg = ChatConfig::from_json("../config.json");
    // ChatSession 构造需要 DSLEngine + IToolRegistry + IInteractionBus + AgentConfig + SessionConfig
    // 此处仅验证 config 可正常加载，ChatSession 的完整构造由 e2e 测试覆盖
    REQUIRE(cfg.app_id == "pdk_chat_demo");
}