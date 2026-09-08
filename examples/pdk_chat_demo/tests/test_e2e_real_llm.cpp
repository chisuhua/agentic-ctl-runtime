// tests/test_e2e_real_llm.cpp
// pdk_chat_demo 真实 LLM 端到端集成测试 (DeepSeek 优先，回退 MINIMAX)
// 关联: docs/examples/pdk_chat_demo/DESIGN.md §8.1 "Real LLM"

#include "catch_amalgamated.hpp"

#include "chat_session.h"
#include "event_handler.h"
#include "test_helpers/real_llm_env.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>
#include <stop_token>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include <agenticdsl/contract/iinteraction_bus.h>
#include <agenticdsl/contract/itool_registry.h>
#include <agenticdsl/contract/inmemory_bus.h>
#include <agenticdsl/contract/iprovider_factory.h>
#include <agenticdsl/plugin/plugin_loader.h>
#include <core/engine.h>
#include <core/types/tool_result.h>
#include <common/llm/llm_config.h>
#include <common/llm/llm_provider_factory.h>
#include <common/llm/llm_types.h>
#include <agenticdsl/types/layered_context.h>

using namespace pdk_chat_demo;
namespace fs = std::filesystem;

static std::string find_loop_agent_so() {
#ifdef LOOP_AGENT_SO_PATH
    const char* p = LOOP_AGENT_SO_PATH;
    if (fs::exists(p)) return fs::canonical(p).string();
#endif
    for (const char* c : {"../../pdk/loop_agent/libLoopAgent.so",
                          "../pdk/loop_agent/libLoopAgent.so",
                          "pdk/loop_agent/libLoopAgent.so"}) {
        if (fs::exists(c)) return fs::canonical(c).string();
    }
    for (auto p = fs::current_path(); p != p.root_path(); p = p.parent_path()) {
        auto candidate = p / "pdk" / "loop_agent" / "libLoopAgent.so";
        if (fs::exists(candidate)) return fs::canonical(candidate).string();
    }
    throw std::runtime_error("libLoopAgent.so not found");
}

static std::string find_loop_dir() {
    if (const char* env = std::getenv("HYDRAFORGE_LOOP_DIR")) return env;
    for (auto p = fs::current_path(); p != p.root_path(); p = p.parent_path()) {
        auto candidate = p / "lib" / "loop";
        if (fs::exists(candidate)) return candidate.string();
    }
    throw std::runtime_error("lib/loop directory not found");
}

static std::string ptr_to_str(void* p) {
    std::ostringstream ss;
    ss << reinterpret_cast<uintptr_t>(p);
    return ss.str();
}

TEST_CASE("Real LLM: deepseek-v4-flash responds to a simple prompt", "[e2e][realllm]") {
    pdk_chat_demo::testing::require_real_llm_env();

    auto cfg = pdk_chat_demo::testing::real_llm_config();
    const std::string& provider     = cfg.provider;
    const std::string& model        = cfg.model;
    const std::string& api_url      = cfg.api_url;
    const std::string& api_endpoint = cfg.api_endpoint;
    const std::string& env_used     = cfg.env_used;
    INFO("Using provider=" << provider << " model=" << model
         << " via " << env_used);

    agenticdsl::LLMConfig llm_cfg;
    llm_cfg.provider     = provider;
    llm_cfg.model        = model;
    llm_cfg.api_url      = api_url;
    llm_cfg.api_endpoint = api_endpoint;
    llm_cfg.api_key      = cfg.api_key;
    llm_cfg.max_tokens    = 512;
    llm_cfg.temperature   = 0.7f;
    llm_cfg.timeout_seconds = 30;
    llm_cfg.max_retries   = 1;

    agenticdsl::LLMProviderFactory factory;
    auto llm = factory.create(llm_cfg);
    REQUIRE(llm != nullptr);

    agenticdsl::GenerationRequest req("Say hello in one short sentence.");
    req.params.model = model;
    req.params.max_tokens = 512;
    req.params.temperature = 0.7;

    auto result = llm->generate(req, std::stop_token{});

    if (!result.has_value()) {
        auto& err = result.error();
        FAIL("LLM generate failed: code=" << static_cast<int>(err.code)
             << " msg=" << err.message);
    }
    REQUIRE(result.has_value());
    auto& gv = result.value();
    REQUIRE_FALSE(gv.text.empty());
    bool contains_greeting = false;
    std::string lower;
    for (char c : gv.text) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lower.find("hello") != std::string::npos ||
        lower.find("hi") != std::string::npos ||
        lower.find("你好") != std::string::npos) {
        contains_greeting = true;
    }
    REQUIRE(contains_greeting);
    INFO("response: " << gv.text);
    INFO("prompt_tokens: " << gv.prompt_tokens << ", completion_tokens: " << gv.completion_tokens);
}

TEST_CASE("Real LLM: ChatSession with deepseek responds to user input", "[e2e][realllm][chat]") {
    pdk_chat_demo::testing::require_real_llm_env();

    auto cfg = pdk_chat_demo::testing::real_llm_config();
    const std::string& provider     = cfg.provider;
    const std::string& model        = cfg.model;
    const std::string& api_url      = cfg.api_url;
    const std::string& api_endpoint = cfg.api_endpoint;

    setenv("HYDRAFORGE_LOOP_DIR", find_loop_dir().c_str(), 1);

    hydraforge::PluginLoader loader;
    auto engine = std::make_unique<agenticdsl::DSLEngine>(
        std::vector<agenticdsl::ParsedGraph>{});
    auto bus = std::make_shared<agenticdsl::InMemoryBus>();

    engine->set_interaction_bus(bus);

    REQUIRE(loader.load_so(find_loop_agent_so(), engine->get_tool_registry()));

    agenticdsl::LLMConfig llm_cfg;
    llm_cfg.provider     = provider;
    llm_cfg.model        = model;
    llm_cfg.api_url      = api_url;
    llm_cfg.api_endpoint = api_endpoint;
    llm_cfg.api_key      = cfg.api_key;
    llm_cfg.max_tokens    = 512;
    llm_cfg.temperature   = 0.7f;
    llm_cfg.timeout_seconds = 30;
    llm_cfg.max_retries   = 1;

    agenticdsl::LLMProviderFactory factory;
    auto llm = factory.create(llm_cfg);
    auto* llm_raw = llm.get();
    engine->set_llm_provider(std::move(llm));

    auto setup = engine->get_tool_registry().call_tool(
        "loop/set_parent_provider",
        std::unordered_map<std::string, std::string>{
            {"provider_ptr", ptr_to_str(llm_raw)}});
    REQUIRE(setup.value("success", false) == true);

    AgentConfig agent_cfg;
    agent_cfg.provider     = provider;
    agent_cfg.model        = model;
    agent_cfg.system_prompt = "You are a helpful assistant.";
    agent_cfg.max_steps    = 1;
    agent_cfg.timeout_ms   = 60000;
    agent_cfg.budget_limit_usd = 0.1;

    SessionConfig session_cfg;
    session_cfg.persist_dir = "";

    ChatSession session(engine.get(), bus, &engine->get_tool_registry(),
                        agent_cfg, session_cfg);

    auto result = session.chat("Say hello in one short sentence.");

    if (!result.success) {
        FAIL("ChatSession chat failed: " << result.error_message);
    }
    REQUIRE(result.success);
    REQUIRE_FALSE(result.response.empty());
    bool contains_greeting = false;
    std::string lower;
    for (char c : result.response) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lower.find("hello") != std::string::npos ||
        lower.find("hi") != std::string::npos ||
        lower.find("你好") != std::string::npos) {
        contains_greeting = true;
    }
    REQUIRE(contains_greeting);
    REQUIRE(result.total_steps >= 1);
    INFO("response: " << result.response);
    INFO("tokens: " << result.total_tokens);
}