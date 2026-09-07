// main.cpp - PDK Chat Demo 入口
// 关联: docs/examples/pdk_chat_demo/DESIGN.md
//      docs/adr/adr-0052 ~ adr-0065

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "chat_session.h"
#include "cli_args_parser.h"
#include "dsl_validator.h"
#include "event_handler.h"

// HydraForge AgenticOS 核心
#include <core/engine.h>
#include <agenticdsl/types/layered_context.h>
#include <core/types/tool_result.h>
#include <core/types/budget.h>
#include <common/llm/llm_types.h>
#include <common/llm/mock_provider.h>
#include <common/llm/llm_config.h>
#include <common/llm/llm_provider_factory.h>
#include <agenticdsl/contract/iinteraction_bus.h>
#include <agenticdsl/contract/inmemory_bus.h>
#include <agenticdsl/contract/itool_registry.h>
#include <agenticdsl/contract/event_builder.h>
#include <agenticdsl/plugin/plugin_loader.h>
#include <modules/budget/budget_controller.h>

#include <agenticdsl/skill/skill_interpreter.h>

#include <core/session_manager.h>

// adr-0070: DECLARE_COMMAND 治理路径 (L4 用户输入层入口, 区别于 L2 Tool)
#include <common/tools/command_registry.h>
#include <common/tools/tool_coordinator.h>
#include <common/policy/agent_mode_policy.h>
#include "commands/command_globals.h"
#include "commands/help_command.h"
#include "commands/compact_command.h"
#include "commands/model_command.h"
#include "commands/tree_command.h"
#include "commands/fork_command.h"
#include "commands/clone_command.h"
#include "tools/provider_switch_stub.h"
#include "tools/session_fork.h"
#include "tools/session_clone.h"

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

hydraforge::PluginLoader* g_loader = nullptr;
agenticdsl::IInteractionBus* g_bus = nullptr;

// Async-signal-safe shutdown flag. Set by signal_handler, observed by main loop.
// MUST be initialized before std::signal() is called at line 462-463.
std::atomic<bool> g_shutdown_requested{false};

void unload_all_plugins(hydraforge::PluginLoader& loader) {
    for (const auto& info : loader.list_loaded()) {
        loader.unload_plugin(std::string(info.name));
    }
}

void signal_handler(int /*sig*/) {
    // 仅设置 shutdown flag，触发由 main 线程在循环观察点执行。
    // 禁止调用 unload_all_plugins() / std::exit() —— 任何非 async-signal-safe
    // 操作都会绕过 engine.h:199-205 的成员析构顺序保证（plugin_loader_ 先于
    // tool_registry_ 声明 → 反向析构时 tool_registry_ 先析构 → ToolRegistry
    // 隐式析构 std::function 回调目标时 plugin .so 已被 dlclose() → SIGSEGV）。
    // 审计依据：docs/audits/2026-08-08-chat-async-io-steering-pre-approval.md
    g_shutdown_requested.store(true, std::memory_order_release);
}

struct StartupCleanupGuard {
    std::unique_ptr<agenticdsl::DSLEngine>* engine = nullptr;
    hydraforge::PluginLoader* loader = nullptr;
    bool active = true;

    void reset_engine() {
        if (!active || engine == nullptr || loader == nullptr) return;
        engine->reset();
        unload_all_plugins(*loader);
        active = false;
    }

    ~StartupCleanupGuard() {
        if (!active || engine == nullptr || loader == nullptr) return;
        engine->reset();
        unload_all_plugins(*loader);
    }
};

}  // namespace

int main(int argc, char* argv[]) {
    // === SkillInterpreter 子进程早期分支（在 DSLEngine 初始化之前） ===
    if (argc > 1 && std::string(argv[1]) == "--skill-child") {
        return agenticdsl::skill_child_main(argc, argv);
    }

    const auto cli = pdk_chat_demo::parse_cli_args(argc, argv);
    if (!cli.ok) { std::cerr << "[main] " << cli.error << std::endl; return 2; }
    if (cli.show_help) { std::cout << cli.help << std::endl; return 0; }
    const auto& cli_options = cli.options;
    const bool mock_mode = cli_options.mock;
    const std::string& session_id_to_load = cli_options.session_id;

    // ============================================================
    // Mock-mode hard rejection（spec Requirement: Mock-mode hard rejection）
    // mock 数据会污染蒸馏训练集 — --allow-training-capture 要求真实 LLM provider
    // ============================================================
    try {
      if (cli_options.allow_training_capture && mock_mode) {
        throw std::runtime_error(
            "--allow-training-capture requires real LLM provider (not mock). "
            "Mock-generated data would pollute the distillation training set.");
      }
    } catch (const std::exception& e) {
      std::cerr << "[main] " << e.what() << std::endl;
      return 1;
    }

    // ============================================================
    // 1.5. Session 选项早期校验 — 在 engine/validation 初始化之前失败快速
    //     --fork 需要 --session 指定源 session
    //     --name 与 --session 互斥（--name 仅用于新建 session）
    // ============================================================
    if (!cli_options.fork_node_id.empty()) {
        if (cli_options.session_id.empty()) {
            std::cerr << "[main] Error: --fork " << cli_options.fork_node_id
                      << " requires --session to specify source session" << std::endl;
            std::cerr << "Use --help for usage." << std::endl;
            return 1;
        }
        // fork_node_id 存在性在 SessionManager open 后检查（见后续逻辑）
    }
    if (!cli_options.session_name.empty() && !cli_options.session_id.empty()) {
        std::cerr << "[main] Error: --name cannot be used with --session (it only applies to new sessions)" << std::endl;
        std::cerr << "Use --help for usage." << std::endl;
        return 1;
    }

    // ============================================================
    // 1. 解析配置
    // ============================================================
    pdk_chat_demo::ChatConfig config;
    try {
        config = pdk_chat_demo::ChatConfig::from_json("config.json");
        if (!cli_options.provider.empty()) config.agent.provider = cli_options.provider;
        config.validate();
        if (mock_mode) {
            config.override_provider("mock", "test");
            std::cout << "[main] Mock mode: provider=mock, model=test" << std::endl;
        } else {
            std::cout << "[main] Live mode: provider=" << config.agent.provider
                      << ", model=" << config.agent.model << std::endl;
        }
        if (!cli_options.system_prompt.empty() || !cli_options.append_system_prompt.empty()) {
            config.override_system_prompt(cli_options.system_prompt,
                                           cli_options.append_system_prompt);
            std::cout << "[main] System prompt overridden (len="
                      << config.agent.system_prompt.size() << ")" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[main] Failed to load config: " << e.what() << std::endl;
        return 1;
    }

    // ============================================================
    // 2. 初始化 AgenticOS (Option 2: 使用 engine 内部 ToolRegistry)
    // ============================================================
    auto engine = std::make_unique<agenticdsl::DSLEngine>(
        std::vector<agenticdsl::ParsedGraph>{});
    StartupCleanupGuard guard;
    guard.engine = &engine;
    guard.active = false;  // 初始化为 false，在 engine 完全就绪后开启
    auto bus = std::make_shared<agenticdsl::InMemoryBus>();

    // 使用 ExecutionBudget 配置预算
    agenticdsl::ExecutionBudget budget_cfg;
    budget_cfg.max_llm_calls = static_cast<int>(config.agent.budget_limit_usd * 1000);
    budget_cfg.max_duration_sec = config.agent.timeout_ms / 1000;
    engine->get_budget_controller().set_budget(std::move(budget_cfg));

    engine->set_interaction_bus(bus);
    g_bus = bus.get();

    // ============================================================
    // 3. 加载所有 Plugin
    // ============================================================

    static constexpr std::string_view kPluginPathPrefix = "/pdk/";
    {
        std::string plugin_root;
        for (const auto& plugin_cfg : config.plugins) {
            if (plugin_cfg.type == "so") {
                auto pos = plugin_cfg.path.find(kPluginPathPrefix);
                if (pos != std::string::npos) {
                    plugin_root = plugin_cfg.path.substr(0, pos + kPluginPathPrefix.size());
                    break;
                }
            }
        }
        if (!plugin_root.empty()) {
            setenv("HYDRAFORGE_PLUGIN_PATH", plugin_root.c_str(), 1);
            std::cout << "[main] HYDRAFORGE_PLUGIN_PATH=" << plugin_root << std::endl;
        }
    }

    hydraforge::PluginLoader loader;
    g_loader = &loader;
    guard.loader = &loader;

    for (const auto& plugin_cfg : config.plugins) {
        if (plugin_cfg.type == "so") {
            try {
                if (!fs::exists(plugin_cfg.path)) {
                    std::cerr << "[main] Plugin not found: " << plugin_cfg.path
                              << " (skipping, demo mode)" << std::endl;
                    continue;
                }
                if (!loader.load_so(plugin_cfg.path, engine->get_tool_registry())) {
                    std::cerr << "[main] FAILED to load plugin: " << plugin_cfg.id
                              << " from " << plugin_cfg.path
                              << " (see PluginLoader WARN/ERROR above)" << std::endl;
                    continue;
                }
                std::cout << "[main] Loaded plugin: " << plugin_cfg.id
                          << " from " << plugin_cfg.path << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[main] Failed to load plugin " << plugin_cfg.id
                          << ": " << e.what() << std::endl;
            }
        } else if (plugin_cfg.type == "skill") {
            // 检查文件后缀决定加载方式
            if (plugin_cfg.path.size() > 9 &&
                plugin_cfg.path.substr(plugin_cfg.path.size() - 9) == ".skill.md") {
                // 命令式 .skill.md — 使用 SkillInterpreter 真实加载
                static bool skill_interpreter_initialized = false;
                static std::unique_ptr<agenticdsl::SkillInterpreter> si;
                if (!skill_interpreter_initialized) {
                    si = std::make_unique<agenticdsl::SkillInterpreter>(
                        engine->get_tool_registry(),
                        *bus,
                        engine->get_llm_provider(),
                        nullptr);
                    skill_interpreter_initialized = true;
                }
                auto cap = agenticdsl::default_skill_capability();
                auto result = si->run(plugin_cfg.path, cap);
                if (result.success) {
                    std::cout << "[main] Skill executed: " << plugin_cfg.id
                              << " (output: " << result.output.dump() << ")"
                              << std::endl;
                } else {
                    std::cerr << "[main] Skill FAILED: " << plugin_cfg.id
                              << " (error=" << static_cast<int>(result.error_code)
                              << ", stderr=" << result.stderr_content << ")"
                              << std::endl;
                }
            } else {
                // 旧式 SKILL.md LLM prompt template — 保持 mock-only
                std::cout << "[main] Skill registered (mock-only, requires SkillInterpreter ADR-0055): "
                          << plugin_cfg.id << std::endl;
            }
        }
    }

    // ============================================================
    // 4. 注册 provider configs
    // ============================================================
    try {
        // provider/register handler 解析 args["args"] 为 JSON
        // 见 pdk/provider_agent/src/pdk_entry.cpp:70
        std::unordered_map<std::string, std::string> provider_map;
        provider_map["args"] = config.providers.dump();
        engine->get_tool_registry().call_tool("provider/register", provider_map);
    } catch (const std::exception& e) {
        std::cerr << "[main] provider/register failed: " << e.what() << std::endl;
    }

    // ============================================================
    // 5. 解析默认 provider → 设置 LLM
    // ============================================================
    std::unique_ptr<agenticdsl::ILLMProvider> llm_provider;

    if (mock_mode) {
        // 直接使用 MockLLMProvider
        llm_provider = std::make_unique<agenticdsl::MockLLMProvider>();
        std::cout << "[main] Using MockLLMProvider" << std::endl;
    } else {
        // 通过 provider/resolve 工具获取 LLMConfig
        try {
            std::unordered_map<std::string, std::string> resolve_args;
            resolve_args["provider_id"] = config.agent.provider;
            resolve_args["model_id"] = config.agent.model;
            auto llm_cfg_json = engine->get_tool_registry().call_tool("provider/resolve", resolve_args);

            // 手动构造 LLMConfig（LLMConfig::from_json 不存在）
            agenticdsl::LLMConfig llm_cfg;
            llm_cfg.provider = llm_cfg_json.value("provider", config.agent.provider);
            llm_cfg.model = llm_cfg_json.value("model", config.agent.model);
            if (llm_cfg_json.contains("api_url"))
                llm_cfg.api_url = llm_cfg_json["api_url"].get<std::string>();
            if (llm_cfg_json.contains("api_key"))
                llm_cfg.api_key = llm_cfg_json["api_key"].get<std::string>();
            // 某些 provider (如百度千帆) 使用非标准 endpoint，需手动指定
            if (llm_cfg_json.contains("api_endpoint"))
                llm_cfg.api_endpoint = llm_cfg_json["api_endpoint"].get<std::string>();

            agenticdsl::LLMProviderFactory factory;
            llm_provider = factory.create(llm_cfg);
        } catch (const std::exception& e) {
            std::cerr << "[main] LLM setup failed: " << e.what() << std::endl;
            std::cerr << "[main] Falling back to MockLLMProvider" << std::endl;
            llm_provider = std::make_unique<agenticdsl::MockLLMProvider>();
        }
    }

    engine->set_llm_provider(std::move(llm_provider));

    if (mock_mode) {
        auto* mock = dynamic_cast<agenticdsl::MockLLMProvider*>(
            engine->get_llm_provider());
        if (mock) {
            mock->enqueue_response(
                R"({"content":"I'll write a hello world in C++ for you.","tool_calls":[]})");
            mock->enqueue_response(
                R"({"content":"Here's the C++ code:\n\n```cpp\n#include <iostream>\nint main(){ std::cout << \"Hello, World!\" << std::endl; return 0; }\n```","tool_calls":[]})");
        }
    }

    // ============================================================
    // 5.5. 传递 LLM provider 给 Loop Agent (loop-agent-dsl-execution)
    // ============================================================
    {
        auto* provider = engine->get_llm_provider();
        if (provider) {
            std::stringstream ss;
            ss << reinterpret_cast<uintptr_t>(provider);
            std::unordered_map<std::string, std::string> set_provider_args;
            set_provider_args["provider_ptr"] = ss.str();
            auto result = engine->get_tool_registry().call_tool(
                "loop/set_parent_provider", set_provider_args);
            if (result.value("success", false)) {
                std::cout << "[main] Loop Agent provider configured" << std::endl;
            }
        }
    }

        guard.active = true;  // 开启 cleanup guard（后续 early return 会正确清理）

    // ============================================================
    // 5.6. T2: DSL Schema 校验 — 加载 .agent.md → DslValidator → 失败退出
    //     Plugin 已加载完毕, registry 含全部工具; 路径与 pdk_entry.cpp 一致
    //     (lib/loop/<loop_type>.agent.md)
    //
    //     跳过条件：当前 lib/loop/*.agent.md 使用 YAML 格式，validator 仅支持
    //     Markdown bold (**key**: value) 格式 — 见 T2 follow-up:
    //     需扩展 validator 支持 YAML，或统一 .agent.md 格式。
    //     注意：T2 9 个测试 fixture 均使用 Markdown bold 格式，与生产 YAML 不同。
    // ============================================================
    {
#ifndef AGENTICDSL_PROJECT_SOURCE_DIR
#define AGENTICDSL_PROJECT_SOURCE_DIR "."
#endif
        const std::vector<std::string> candidates = {
            "lib/loop/" + config.agent.loop_type + ".agent.md",
            "../lib/loop/" + config.agent.loop_type + ".agent.md",
            std::string(AGENTICDSL_PROJECT_SOURCE_DIR) + "/lib/loop/" +
                config.agent.loop_type + ".agent.md"
        };

        std::string markdown_content;
        std::string agent_md_path;
        for (const auto& path : candidates) {
            std::ifstream md_file(path);
            if (md_file.is_open()) {
                std::stringstream ss;
                ss << md_file.rdbuf();
                markdown_content = ss.str();
                md_file.close();
                agent_md_path = path;
                break;
            }
        }

        if (markdown_content.empty()) {
            std::cerr << "[main] DSL Schema Validation skipped: "
                      << "lib/loop/" << config.agent.loop_type
                      << ".agent.md not found" << std::endl;
        } else if (mock_mode) {
            // Mock mode skips DSL validation — mock provider doesn't need DSL validation
            std::cout << "[main] DSL Schema Validation skipped (mock mode)" << std::endl;
        } else {
            // fix-markdown-parser-yaml: 双格式自动检测 (bold + yaml fenced)
            const agenticdsl::IToolRegistry* registry =
                &engine->get_tool_registry();
            pdk_chat_demo::DslValidator validator;
            auto vr = validator.validate(markdown_content, registry);

            if (!vr.valid) {
                std::cerr << "[main] DSL Schema Validation FAILED for "
                          << agent_md_path << " ("
                          << vr.errors.size() << " errors):" << std::endl;
                for (const auto& e : vr.errors) {
                    std::cerr << "  - [" << e.type << "] " << e.node_path
                              << ": " << e.message << std::endl;
                }
                // Early cleanup: reset engine (destroys ToolRegistry properly) then unload plugins.
                // guard.active=true 确保 guard destructor 不会重复 cleanup (engine 已 reset).
                engine.reset();
                unload_all_plugins(loader);
                guard.active = false;
                return 1;
            }
            std::cout << "[main] DSL Schema Validation OK: " << agent_md_path
                      << std::endl;
        }
    }

    // ============================================================
    // 6. 订阅事件 → 终端输出
    // ============================================================
    pdk_chat_demo::EventHandler handler(bus);

    // ============================================================
    // 7. ChatSession 初始化（使用 engine 内部 registry）
    // ============================================================
    config.session.enable_input_thread = isatty(STDIN_FILENO) != 0;
    // T1.9: 启动时清理 >24h 的 stale session 文件
    pdk_chat_demo::ChatSession::cleanup_stale(config.session.persist_dir);

    pdk_chat_demo::ChatSession session(
        engine.get(), bus, &engine->get_tool_registry(),
        config.agent, config.session
    );

    // T1.4: --session <id> 从磁盘恢复
    if (!session_id_to_load.empty()) {
        if (session.load_from_disk(session_id_to_load)) {
            std::cout << "[main] Session restored: " << session.session_id()
                      << " (" << session.history().size() << " messages)" << std::endl;
        } else {
            std::cout << "[main] Session not found/corrupted, starting new: "
                      << session.session_id() << std::endl;
        }
    }

    std::cout << "[main] Session started: " << session.session_id() << std::endl;
    std::cout << "[main] Type 'exit' or Ctrl-D to quit" << std::endl;

    // ============================================================
    // 7.5 adr-0070: ToolCoordinator 注入 + CommandRegistry 治理路径
    //     Command 层经 ToolCoordinator::execute() 调用工具, 触发 layer check /
    //     ApprovalHandler / audit (ADR-0068) / hook (ADR-0069) 治理路径
    // ============================================================
    auto coordinator = std::make_unique<agenticdsl::ToolCoordinator>(
        engine->get_tool_registry(),
        std::make_shared<agenticdsl::AgentModePolicy>(),
        [](const agenticdsl::ApprovalRequest& req, int /*timeout_ms*/) {
            // demo 默认 auto-approve (用户可通过 /help 查 /compact 用法)
            (void)req;
            return true;
        });
    auto* coord_ptr = coordinator.get();
    engine->set_tool_coordinator(std::move(coordinator));
    if (engine->get_tool_coordinator() == nullptr) {
        std::cerr << "fatal: ToolCoordinator injection failed" << std::endl;
        return 1;
    }
    std::cout << "[main] tool_coordinator: enabled" << std::endl;

    agenticdsl::CommandRegistry command_registry(coord_ptr);
    pdk_chat_demo::g_command_coordinator = coord_ptr;
    pdk_chat_demo::g_command_session = &session;
    pdk_chat_demo::g_command_registry = &command_registry;

    auto session_manager = std::make_unique<agenticdsl::SessionManager>(
        fs::path(config.session.persist_dir));
    // --session <id> 优先，否则使用 "default" session
    const std::string resolved_session_id =
        session_id_to_load.empty() ? std::string("default") : session_id_to_load;
    if (!session_id_to_load.empty() &&
        (session_id_to_load.find('/') != std::string::npos ||
         session_id_to_load.find('\\') != std::string::npos ||
         session_id_to_load == "." || session_id_to_load == "..")) {
        std::cerr << "[main] Error: --session '" << session_id_to_load
                  << "' is not a valid session id" << std::endl;
        std::cerr << "Use --help for usage." << std::endl;
        return 1;
    }
    try {
        session_manager->open(resolved_session_id);
    } catch (const std::exception& e) {
        std::cerr << "[main] Failed to open session '" << resolved_session_id
                  << "': " << e.what() << std::endl;
        return 1;
    }
    pdk_chat_demo::g_session_manager = session_manager.get();

    pdk_chat_demo::register_provider_switch_stub_tool(engine->get_tool_registry());
    pdk_chat_demo::register_session_fork_tool(engine->get_tool_registry());
    pdk_chat_demo::register_session_clone_tool(engine->get_tool_registry());

    command_registry.register_command(pdk_chat_demo::make_help_command_spec());
    command_registry.register_command(pdk_chat_demo::make_compact_command_spec());
    command_registry.register_command(pdk_chat_demo::make_model_command_spec());
    command_registry.register_command(pdk_chat_demo::make_tree_command_spec());
    command_registry.register_command(pdk_chat_demo::make_fork_command_spec());
    command_registry.register_command(pdk_chat_demo::make_clone_command_spec());

    // ============================================================
    // 7.9. 验证 --fork node_id 是否存在于已加载 session
    // ============================================================
    if (!cli_options.fork_node_id.empty() && !session_id_to_load.empty()) {
        auto* node = session_manager->find_node(cli_options.fork_node_id);
        if (node == nullptr) {
            std::cerr << "[main] Error: --fork node '" << cli_options.fork_node_id
                      << "' not found in session '" << session_id_to_load << "'" << std::endl;
            std::cerr << "Use --help for usage." << std::endl;
            return 1;
        }
    }

    std::cout << std::endl << "User> " << std::flush;

    // ============================================================
    // 8. 信号处理 + 交互循环
    // ============================================================
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::string input;
    while (std::getline(std::cin, input)) {
        // 优先检查 shutdown flag —— signal_handler 仅置位，实际清理在 main 线程执行。
        if (g_shutdown_requested.load(std::memory_order_acquire)) {
            break;
        }
        if (!input.empty() && input.front() == '/') {
            if (input == pdk_chat_demo::kExitCommand ||
                input.rfind(std::string(pdk_chat_demo::kExitCommand) + " ", 0) == 0) {
                break;
            }
            auto spec = command_registry.resolve_command(input);
            if (spec) {
                hydraforge::pdk::CommandContext ctx;
                ctx.user_input = input;
                ctx.tool_coordinator = coord_ptr;
                pdk_chat_demo::g_current_command_input = input;
                std::string output;
                try {
                    output = spec->handler(ctx.tool_ctx);
                } catch (const std::exception& e) {
                    output = std::string("[command error] ") + e.what();
                }
                if (output == pdk_chat_demo::kCommandExitSentinel) {
                    break;
                }
                std::cout << output << std::endl;
            } else {
                std::string name;
                auto pos = input.find(' ');
                auto part = (pos == std::string::npos) ? input.substr(1)
                                                       : input.substr(1, pos - 1);
                if (!part.empty()) name = part;
                std::cout << "unknown command: /" << name
                          << ". Type /help for list of commands." << std::endl;
            }
            std::cout << std::endl << "User> " << std::flush;
            continue;
        }
        if (input.empty()) {
            std::cout << std::endl << "User> " << std::flush;
            continue;
        }

        auto result = session.chat(input);

        // T1 线程安全: 检查 budget alert atomic flag (dispatch 线程置位)
        if (session.consume_budget_alert()) {
            std::cerr << std::endl << "[⚠ Budget exceeded] cost limit reached" << std::endl;
        }

        if (result.success) {
            std::cout << std::endl << "Assistant: " << result.response << std::endl;
            std::cout << "  [steps=" << result.total_steps
                      << ", tokens=" << result.total_tokens
                      << ", cost=$" << result.cost_usd << "]" << std::endl;
        } else {
            std::cerr << std::endl << "[error] " << result.error_message << std::endl;
        }

        std::cout << std::endl << "User> " << std::flush;
    }

    // T1.3.3: shutdown 前显式检查 budget alert flag (防止 dispatch 未完成)
    if (session.consume_budget_alert()) {
        std::cerr << std::endl << "[⚠ Budget exceeded] final check on shutdown" << std::endl;
    }

    // T1: 落盘当前 session
    session.save_to_disk();

    // ============================================================
    // 9. 优雅退出 — 先销毁 ChatSession + DSLEngine（释放 ToolRegistry
    //    中的 plugin function ptr），再 unload plugin .so，避免
    //    dangling function pointer → SIGSEGV
    // ============================================================
    bus->emit(agenticdsl::EventBuilder("app.shutdown").build());
    // 跳出局部 scope 以销毁 ChatSession 和 DSLEngine
    //（它们的析构函数会清理 ToolRegistry 中的 plugin 引用）
    {
        pdk_chat_demo::ChatSession discard(nullptr, nullptr, nullptr, {}, {});
        guard.reset_engine();
    }
    unload_all_plugins(loader);
    std::cout << std::endl << "[main] Goodbye!" << std::endl;

    return 0;
}
