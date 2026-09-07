// PDK Plugin entry — AgenticDSL v1 API (hydraforge namespace for PluginInfo, agenticdsl for IToolRegistry)
// 关联: openspec/changes/2026-07-17-pdk-chat-demo-buildable/

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <atomic>

#include <nlohmann/json.hpp>

#include <agenticdsl/contract/bus_event.h>
#include <agenticdsl/contract/iinteraction_bus.h>
#include <agenticdsl/contract/itool_registry.h>
#include <agenticdsl/plugin/plugin_info.h>
#include <agenticdsl/types/layered_context.h>
#include <core/engine.h>

// §4.0.4 chat-async-io-consumer-loop: use SHARED CancellationRegistry from pdk_chat_demo
// (was: file-static g_loop_registry; now: g_cancellation_registry global, same identity as ChatSession)
#include "cancellation_registry.h"
#include "commands/cancellation_globals.h"

namespace fs = std::filesystem;

namespace {

// ADR-0068 附录 A 事件发射 helper (Decision 4/5): 未注入 bus 时静默跳过
inline void emit_loop_event(::agenticdsl::IInteractionBus* bus,
                            const std::string& session_id,
                            const std::string& topic,
                            nlohmann::json payload) {
    if (!bus) return;
    payload["session_id"] = session_id;
    bus->emit(::agenticdsl::BusEvent{
        topic, ::agenticdsl::ToolResult{.ok = true, .meta = std::move(payload)}});
}

inline nlohmann::json json_arg(const std::unordered_map<std::string, std::string>& args,
                                const std::string& key) {
    auto it = args.find(key);
    if (it == args.end()) return nlohmann::json();
    try {
        return nlohmann::json::parse(it->second);
    } catch (...) {
        return it->second;
    }
}

inline std::string str_arg(const std::unordered_map<std::string, std::string>& args,
                           const std::string& key, const std::string& default_val = "") {
    auto it = args.find(key);
    return (it != args.end()) ? it->second : default_val;
}

inline int int_arg(const std::unordered_map<std::string, std::string>& args,
                   const std::string& key, int default_val = 0) {
    auto it = args.find(key);
    if (it == args.end()) return default_val;
    try { return std::stoi(it->second); } catch (...) { return default_val; }
}

// 找到 lib/loop/ 目录路径
fs::path find_loop_dir() {
    // 1. 环境变量
    const char* env_path = std::getenv("HYDRAFORGE_LOOP_DIR");
    if (env_path) return env_path;

    // 2. 当前工作目录相对路径
    return fs::current_path() / "lib" / "loop";
}

std::string load_agent_file(const std::string& loop_type) {
    fs::path loop_dir = find_loop_dir();
    fs::path agent_file = loop_dir / (loop_type + ".agent.md");

    if (!fs::exists(agent_file)) {
        throw std::runtime_error(
            "Loop Agent file not found: " + agent_file.string()
        );
    }

    std::ifstream f(agent_file);
    if (!f.is_open()) {
        throw std::runtime_error(
            "Cannot open loop file: " + agent_file.string()
        );
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

// loop-agent-dsl-execution: thread_local storage for parent engine's LLM provider
// Uses thread_local for per-thread isolation (multi-engine scenarios).
// nullptr = not set, mock fallback path.
static thread_local ::agenticdsl::ILLMProvider* tls_parent_provider = nullptr;

// §4.0.4 chat-async-io-consumer-loop: REMOVED file-static g_loop_registry
// Now uses pdk_chat_demo::g_cancellation_registry (same identity as ChatSession)

// --- pdk_plugin_info ---
extern "C" const hydraforge::PluginInfo pdk_plugin_info = {
    hydraforge::CURRENT_ABI_VERSION,                                  // abi_version = 2
    "chat.loop",                                                      // name[64]
    0, 1, 0,                                                          // semver major.minor.patch
    "Loop Agent - React/PlanExecute/ForkJoin DSL executor",           // description[256]
    "react_loop,plan_execute_loop,fork_join_loop",                    // capabilities[512]
    ""                                                                // dependencies[256]
};

// --- pdk_register_tools ---
extern "C" void pdk_register_tools(::agenticdsl::IToolRegistry& registry) {
    // loop/set_parent_provider — 配置父引擎 LLM provider 引用
    // force_approval_always=true, allowed_layers={Workflow} (仅 hand-written DSL 可调用)
    registry.register_tool_function(
        "loop/set_parent_provider",
        ::agenticdsl::ToolMetadata{
            .name = "loop/set_parent_provider",
            .description = "Set parent engine LLM provider for loop agent DSL execution",
            .domain = "loop",
            .category = ::agenticdsl::ToolCategory::StateModify,
            .min_layer = ::agenticdsl::LayerProfile::Workflow,
            .approval = ::agenticdsl::ApprovalPolicy{
                .requires_approval_in_plan = false,
                .requires_approval_in_agent = true,
                .requires_approval_in_yolo = false,
                .force_approval_always = true
            },
            .allowed_layers = {::agenticdsl::LayerProfile::Workflow}
        },
        [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
            auto it = args.find("provider_ptr");
            if (it == args.end() || it->second.empty()) {
                return {{"success", false}, {"error", "Missing provider_ptr argument"}};
            }
            try {
                auto* new_provider = reinterpret_cast<::agenticdsl::ILLMProvider*>(
                    std::stoull(it->second));
                if (!new_provider) {
                    return {{"success", false}, {"error", "Null provider_ptr"}};
                }
                if (tls_parent_provider && tls_parent_provider != new_provider) {
                    std::cerr << "[loop_agent] WARNING: overwriting parent provider "
                              << tls_parent_provider << " → " << new_provider << std::endl;
                }
                tls_parent_provider = new_provider;
                return {{"success", true}};
            } catch (const std::exception& e) {
                return {{"success", false}, {"error", std::string("Invalid provider_ptr: ") + e.what()}};
            }
        }
    );

    // 注册 loop/run 工具
    registry.register_tool_function(
        "loop/run",
        ::agenticdsl::ToolMetadata{
            .name = "loop/run",
            .description = "Run an agent loop from a .agent.md file",
            .domain = "loop",
            .category = ::agenticdsl::ToolCategory::Execute,
            .min_layer = ::agenticdsl::LayerProfile::Workflow,
            .approval = ::agenticdsl::ApprovalPolicy{
                .requires_approval_in_plan = false,
                .requires_approval_in_agent = true,
                .requires_approval_in_yolo = false,
                .force_approval_always = false
            },
            .allowed_layers = {::agenticdsl::LayerProfile::Workflow}
        },
        [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
            std::string loop_type = str_arg(args, "loop_type", "react");
            std::string user_prompt = str_arg(args, "prompt");

            // loop_type 合法性校验 (Q7: 仅 react/plan_execute/fork_join)
            if (loop_type != "react" && loop_type != "plan_execute" && loop_type != "fork_join") {
                return {{"success", false},
                        {"error", "Invalid loop_type: '" + loop_type +
                         "'. Must be one of: react, plan_execute, fork_join"}};
            }

            // 可选 bus 注入 (Decision 5): 未传 bus_ptr 则跳过事件发射, 不影响返回
            ::agenticdsl::IInteractionBus* bus = nullptr;
            {
                auto bus_it = args.find("bus_ptr");
                if (bus_it != args.end() && !bus_it->second.empty()) {
                    bus = reinterpret_cast<::agenticdsl::IInteractionBus*>(
                        std::stoull(bus_it->second));
                }
            }
            std::string session_id = str_arg(args, "session_id");

            // §4.0.10 chat-async-io-consumer-loop: resolve from shared g_cancellation_registry
            // Null-guard: nullptr global → non-cancellable-but-executable (no error, no crash)
            std::string cancellation_id = str_arg(args, "cancellation_id");
            std::stop_token cancellation_token;
            if (!cancellation_id.empty() && pdk_chat_demo::g_cancellation_registry) {
                cancellation_token =
                    pdk_chat_demo::g_cancellation_registry->resolve_token(cancellation_id);
            }

            // Mock fallback when parent provider not set (Q3/Q7)
            if (!tls_parent_provider) {
                nlohmann::json output;
                output["response"] =
                    "[loop_agent/" + loop_type + "] Processed: \"" + user_prompt + "\"\n\n"
                    "Mock fallback: parent LLM provider not set. Call loop/set_parent_provider first, "
                    "or this will return the legacy mock response.";
                output["steps"] = 1;
                output["tokens_used"] = 42;
                output["cost_usd"] = 0.001;
                output["success"] = true;
                return output;
            }

            // Phase B Step 3: 收到取消请求时提前返回
            if (cancellation_token.stop_requested()) {
                nlohmann::json cancelled_result;
                cancelled_result["success"] = false;
                cancelled_result["error"] = "cancelled";
                cancelled_result["response"] = "";
                cancelled_result["steps"] = 0;
                cancelled_result["tokens_used"] = 0;
                cancelled_result["cost_usd"] = 0.0;
                return cancelled_result;
            }

            // Real DSL execution path (Q4: errors propagate via return)
            try {
                auto agent_content = load_agent_file(loop_type);

                auto child = ::agenticdsl::DSLEngine::from_markdown(
                    agent_content, *tls_parent_provider);

                // react.agent.md 等模板使用 llm_call 节点 (默认工具名 "llama-default").
                // 子引擎 registry 与父引擎隔离, 因此此处用 borrowed provider 包装一个 LLM 工具注入.
                // 默认模型名沿用父 provider 注册的模型列表首位, 避免默认 "gpt-4o-mini" 在非 OpenAI 端点上失败
                class ProviderLLMTool : public ::agenticdsl::ILLMTool {
                 public:
                    ProviderLLMTool(::agenticdsl::ILLMProvider& p, std::stop_token tok)
                        : provider_(p), cancellation_token_(std::move(tok)) {}
                    ::agenticdsl::LLMResult generate(
                        const std::string& prompt,
                        const ::agenticdsl::LLMParams& params) override {
                        ::agenticdsl::LLMResult out;
                        ::agenticdsl::GenerationRequest req(prompt);
                        req.params = params;
                        auto avail = provider_.available_models();
                        if (!avail.empty()) {
                            req.params.model = avail.front().name;
                        }
                        auto res = provider_.generate(req, cancellation_token_);
                        if (res.has_value()) {
                            out.success = true;
                            out.text = std::move(res).value().text;
                            out.tokens_generated = res.value().completion_tokens;
                        } else {
                            out.success = false;
                            out.error = res.error().message;
                        }
                        return out;
                    }
                    bool is_available() const override { return true; }
                    std::string name() const override { return "loop-agent-provider-bridge"; }
                 private:
                    ::agenticdsl::ILLMProvider& provider_;
                    std::stop_token cancellation_token_;
                };
                child->register_llm_tool(
                    "llama-default",
                    std::make_unique<ProviderLLMTool>(*tls_parent_provider, cancellation_token));

                ::agenticdsl::LayeredContext ctx;
                ctx.working["user_input"] = user_prompt;
                ctx.working["system_prompt"] = str_arg(args, "system_prompt");
                ctx.working["history"] = str_arg(args, "history");

                // ADR-0068 附录 A: loop.turn.start {turn, step}
                emit_loop_event(bus, session_id, "loop.turn.start",
                                {{"turn", 1}, {"step", 1}});

                // ADR-0068 附录 A: loop.decision {decision, tool?}
                emit_loop_event(bus, session_id, "loop.decision",
                                {{"decision", "tool_call"}, {"tool", "loop/run"}});

                auto result = child->run(ctx);

                // ADR-0068 附录 A: loop.turn.end {turn, decision}
                emit_loop_event(bus, session_id, "loop.turn.end",
                                {{"turn", 1},
                                 {"decision", result.success ? "respond" : "give_up"}});

                nlohmann::json output;
                output["success"] = result.success;
                output["error"]   = result.success ? "" : result.message;
                std::string response_text;
                for (const char* k : {"response", "output",
                                      "llm_response", "plan_response",
                                      "final_result"}) {
                    auto v = result.final_context.find(k);
                    if (v != result.final_context.end() && v->is_string() &&
                        !v->get<std::string>().empty()) {
                        response_text = v->get<std::string>();
                        break;
                    }
                }
                if (response_text.empty()) response_text = result.message;
                output["response"] = response_text;
                output["steps"]  = 1;
                output["tokens_used"] = 0;
                output["cost_usd"]    = 0.0;
                return output;
            } catch (const std::exception& e) {
                return {{"success", false}, {"error", e.what()},
                        {"response", ""}, {"steps", 0}, {"tokens_used", 0}, {"cost_usd", 0.0}};
            }
        }
    );
}