// chat_session.cpp - Chat Session 实现
// 关联: chat_session.h, docs/adr/adr-0060-agent-composition.md
//      openspec/changes/pdk-chat-demo-v1-recap/design.md (T1: 持久化 + Budget 告警)

#include "chat_session.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <utility>

#include <core/engine.h>
#include <core/types/tool_result.h>
#include <agenticdsl/types/layered_context.h>
#include <agenticdsl/contract/itool_registry.h>
#include <agenticdsl/contract/bus_event.h>
#include <agenticdsl/contract/event_builder.h>
#include <agenticdsl/contract/iinteraction_bus.h>
#include <modules/budget/budget_controller.h>

// Sprint 30: timer_service.h 提供 ITimerService complete type (Impl 成员访问需完整类型),
// 仅在 chat_session.cpp include (header 用 void* PIMPL 避开 namespace pollution)
#include <agenticdsl/contract/timer_service.h>


namespace pdk_chat_demo {

namespace {

static std::string ptr_to_str(void* p) {
    std::ostringstream ss;
    ss << reinterpret_cast<uintptr_t>(p);
    return ss.str();
}

// T1.2: 展开 ~ 为 HOME 目录 (优先 HOME env, fallback getpwuid)
std::string expand_home(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::string(home) + path.substr(1);
    }
    return path;
}

// T1.10: 创建目录权限 0700
bool ensure_dir_0700(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        std::cerr << "[session] create_directories failed: " << dir
                  << " (" << ec.message() << ")" << std::endl;
        return false;
    }
    if (chmod(dir.c_str(), 0700) != 0) {
        std::cerr << "[session] chmod 0700 failed: " << dir << std::endl;
    }
    return true;
}

constexpr int kSessionSchemaVersion = 1;
constexpr size_t kDefaultQueueCapacity = 32;

}  // namespace

// --- ChatConfig ---

ChatConfig ChatConfig::from_json(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error("Cannot open config: " + path);
    }
    nlohmann::json j;
    f >> j;
    ChatConfig cfg;

    cfg.schema_version = j.value("schema_version", "1.0");
    cfg.app_id = j.value("app_id", "pdk_chat_demo");

    if (j.contains("providers")) cfg.providers = j["providers"];
    if (j.contains("orchestration")) cfg.orchestration = j["orchestration"];
    if (j.contains("safety")) cfg.safety = j["safety"];

    if (j.contains("agent")) {
        auto& a = j["agent"];
        cfg.agent.loop_type = a.value("loop_type", "react");
        cfg.agent.provider = a.value("provider", "mock");
        cfg.agent.model = a.value("model", "test");
        cfg.agent.system_prompt = a.value("system_prompt", "");
        if (a.contains("tools")) {
            for (auto& t : a["tools"]) cfg.agent.tools.push_back(t.get<std::string>());
        }
        cfg.agent.max_steps = a.value("max_steps", 50);
        cfg.agent.timeout_ms = a.value("timeout_ms", 300000);
        cfg.agent.budget_limit_usd = a.value("budget_limit_usd", 1.0);
    }

    if (j.contains("plugins")) {
        for (auto& p : j["plugins"]) {
            PluginConfig pc;
            pc.id = p.value("id", "");
            pc.path = p.value("path", "");
            pc.type = p.value("type", "so");
            pc.lifecycle = p.value("lifecycle", "eager");
            pc.requires_isolation = p.value("requires_isolation", false);
            if (p.contains("activation_events")) {
                for (auto& e : p["activation_events"]) {
                    pc.activation_events.push_back(e.get<std::string>());
                }
            }
            cfg.plugins.push_back(std::move(pc));
        }
    }

    if (j.contains("observability")) {
        auto& o = j["observability"];
        cfg.observability.otel_enabled = o.value("otel_enabled", false);
        cfg.observability.endpoint = o.value("endpoint", "http://localhost:4318");
        cfg.observability.sample_rate = o.value("sample_rate", 1.0);
        cfg.observability.export_format = o.value("export_format", "otlp+http");
    }

    if (j.contains("session")) {
        auto& s = j["session"];
        cfg.session.persist_dir = s.value("persist_dir", "~/.hydraforge/sessions/");
        cfg.session.compact_threshold_tokens = s.value("compact_threshold_tokens", 8000);
        cfg.session.branch_on_user_request = s.value("branch_on_user_request", true);
    }

    return cfg;
}

void ChatConfig::override_provider(const std::string& provider, const std::string& model) {
    this->agent.provider = provider;
    this->agent.model = model;
}

void ChatConfig::override_system_prompt(const std::string& overwrite,
                                        const std::string& append) {
  if (!overwrite.empty()) {
    agent.system_prompt = overwrite;
  }
  if (!append.empty()) {
    if (!agent.system_prompt.empty()) {
      agent.system_prompt += "\n";
    }
    agent.system_prompt += append;
  }
}

void ChatConfig::validate() const {
    if (schema_version != "1.0") {
        throw std::runtime_error("Unsupported schema_version: " + schema_version);
    }
    if (app_id.empty()) {
        throw std::runtime_error("app_id is required");
    }
    if (agent.provider.empty() || agent.model.empty()) {
        throw std::runtime_error("agent.provider and agent.model are required");
    }
    if (agent.max_steps <= 0) {
        throw std::runtime_error("agent.max_steps must be > 0");
    }
    if (agent.timeout_ms <= 0) {
        throw std::runtime_error("agent.timeout_ms must be > 0");
    }
}

// --- ChatSession::Impl ---

class ChatSession::Impl {
public:
    agenticdsl::DSLEngine* engine;
    std::shared_ptr<agenticdsl::IInteractionBus> bus;
    agenticdsl::IToolRegistry* registry;
    AgentConfig agent_cfg;
    SessionConfig session_cfg;
    std::vector<nlohmann::json> messages;
    std::string provider_mode;
    std::string persist_dir_expanded;

    // Cancellation state (Phase B: chat-async-io-cancellation-chain)
    std::shared_ptr<CancellationRegistry> cancellation_registry_;
    std::string current_cancellation_id_;
    std::stop_token current_token_;

    // Wave 3-A Phase C: pending model switch target (next turn will swap to this)
    std::string next_model_;
    mutable std::mutex next_model_mutex_;

    // Async queue infrastructure (Phase A)
    std::queue<std::string> steering_queue_;
    std::queue<std::string> follow_up_queue_;
    mutable std::mutex steering_mutex_;
    mutable std::mutex follow_up_mutex_;
    size_t capacity_ = kDefaultQueueCapacity;

    // chat-async-io-consumer-loop §1.5: condition variable + atomic counter for blocking pop
    std::condition_variable input_cv_;
    std::mutex input_cv_mutex_;
    std::atomic<size_t> pending_input_count_{0};

    // chat-async-io-consumer-loop §5.3: stop_poll_ flag for interrupt_thread RAII GuardChatScope
    std::atomic<bool> stop_poll_{false};

    // Input thread (async producer)
    std::thread input_thread_;
    std::atomic<bool> stop_input_thread_{false};

    // === Sprint 30 timer migration (D1/D3/D5/D9) ===
    // timer_: 观察者指针 (从 void* handle cast, PIMPL void* 模式避开 namespace pollution)
    // periodic_id_: input_thread 注册的 periodic timer id
    // shutdown_check_pending_: timer callback → 主循环通信 (acquire/release 序)
    agenticdsl::ITimerService* timer_ = nullptr;
    agenticdsl::ITimerService::TimerId periodic_id_ = 0;
    std::atomic<bool> shutdown_check_pending_{false};

    Impl(
        agenticdsl::DSLEngine* e,
        std::shared_ptr<agenticdsl::IInteractionBus> b,
        agenticdsl::IToolRegistry* r,
        const AgentConfig& a,
        const SessionConfig& s,
        std::shared_ptr<CancellationRegistry> registry_arg,
        // Sprint 30: PIMPL void* handle (header 用 void* 避开 namespace pollution,
        // cpp 内 cast 回 ITimerService*。 nullptr = 不注入, D9 lazy)
        void* timer_handle = nullptr
    ) : engine(e), bus(std::move(b)), registry(r), agent_cfg(a), session_cfg(s),
        provider_mode(a.provider),
        persist_dir_expanded(expand_home(s.persist_dir)),
        // Sprint 30: cast void* → ITimerService* (调用方保证类型正确)
        timer_(timer_handle ? static_cast<agenticdsl::ITimerService*>(timer_handle) : nullptr),
        // §4.0.2/§4.0.9 NC3: shared registry if provided, else fallback self-owned
        cancellation_registry_(registry_arg ? registry_arg : std::make_shared<CancellationRegistry>()) {
        if (!persist_dir_expanded.empty()) {
            ensure_dir_0700(persist_dir_expanded);
        }
        if (session_cfg.enable_input_thread) {
            input_thread_ = std::thread([this]() { input_thread_main(); });
        }
    }

    ~Impl() {
        // D8 四步析构顺序 (Sprint 29 复用):
        // ① 防御性 cancel periodic timer (idempotent, RAII guard 已 cancel 时 id=0)
        if (periodic_id_ != 0 && timer_) {
            timer_->cancel(periodic_id_);
            periodic_id_ = 0;
        }
        timer_ = nullptr;

        stop_input_thread_.store(true);
        // §2.3/§3.4: notify cv so any blocked pop_next_input wakes and returns nullopt
        input_cv_.notify_all();
        if (input_thread_.joinable()) {
            input_thread_.join();
        }
    }

private:
    void input_thread_main();
};

// --- ChatSession ---

ChatSession::ChatSession(
    agenticdsl::DSLEngine* engine,
    std::shared_ptr<agenticdsl::IInteractionBus> bus,
    agenticdsl::IToolRegistry* registry,
    const AgentConfig& agent_cfg,
    const SessionConfig& session_cfg,
    std::shared_ptr<CancellationRegistry> registry_arg,
    void* timer_handle
) : impl_(std::make_unique<Impl>(engine, std::move(bus), registry, agent_cfg, session_cfg, registry_arg, timer_handle)) {
    // 生成 session ID (UUID 简化版)
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    std::ostringstream oss;
    oss << "sess_" << std::hex << dis(gen) << dis(gen);
    session_id_ = oss.str();

    // T1 线程安全: budget.checked 回调仅置 atomic flag, 不触 TUI
    if (impl_->bus) {
        impl_->bus->subscribe("budget.checked",
            [this](const agenticdsl::BusEvent&) {
                budget_alert_flag_.store(true, std::memory_order_release);
            });
    }
}

ChatSession::~ChatSession() = default;

void ChatSession::request_stop() {
  if (impl_->current_cancellation_id_.empty()) return;
  auto source = impl_->cancellation_registry_->resolve_source(
      impl_->current_cancellation_id_);
  if (source) source->request_stop();
  // §2.3: wake any blocked pop_next_input (cheap, idempotent)
  impl_->input_cv_.notify_all();
}

bool ChatSession::request_model_switch(const std::string& provider_name) {
  if (provider_name.empty()) return false;
  if (impl_->provider_mode == "mock" && provider_name != "mock") {
    std::cerr << "[chat] mock mode rejects provider: " << provider_name
              << std::endl;
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->next_model_mutex_);
  impl_->next_model_ = provider_name;
  return true;
}

std::string ChatSession::next_model() const {
  std::lock_guard<std::mutex> lock(impl_->next_model_mutex_);
  return impl_->next_model_;
}

ChatResult ChatSession::chat(const std::string& user_input) {
    return chat(user_input, std::stop_token{});
}

ChatResult ChatSession::chat(const std::string& user_input, std::stop_token token) {
    ChatResult result;

    // §4.0.5: ALWAYS build stop_source + register, regardless of token.stop_possible()
    std::string cancellation_id;
    auto source = std::make_shared<std::stop_source>();
    cancellation_id = impl_->cancellation_registry_->register_source(source);
    impl_->current_cancellation_id_ = cancellation_id;
    if (token.stop_possible()) {
        impl_->current_token_ = token;
    }

    // 2. 追加用户消息到历史
    nlohmann::json user_msg = {
        {"role", "user"},
        {"content", user_input},
        {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
    };
    impl_->messages.push_back(user_msg);

    // 2. emit "user.input" 事件 (ADR-0068 §4 — args=业务字段, meta=trace context)
    impl_->bus->emit(agenticdsl::EventBuilder("user.input")
        .args(nlohmann::json{{"input", user_input}})
        .meta(nlohmann::json{{"session_id", session_id_}})
        .build());

    // 3. 获取 LLM 响应 — 统一经 Loop Agent 工具 (loop/run) 执行
    //    loop_agent 内部决定 mock fallback (parent provider 未设置) 或真实 DSL 执行
    try {
        // Loop Agent 工具 — 唯一 ReAct 执行路径
        std::unordered_map<std::string, std::string> loop_args;
        loop_args["loop_type"] = impl_->agent_cfg.loop_type;
        loop_args["prompt"] = user_input;
        loop_args["system_prompt"] = impl_->agent_cfg.system_prompt;
        loop_args["history"] = nlohmann::json(impl_->messages).dump();
        loop_args["tools"] = nlohmann::json(impl_->agent_cfg.tools).dump();
        loop_args["max_steps"] = std::to_string(impl_->agent_cfg.max_steps);
        // 将 bus 与会话 ID 透传给 loop_agent, 用于真实事件发射
        loop_args["bus_ptr"] = ptr_to_str(impl_->bus.get());
        loop_args["session_id"] = session_id_;
        loop_args["cancellation_id"] = cancellation_id;

        nlohmann::json loop_result = impl_->registry->call_tool("loop/run", loop_args);

        // Cleanup cancellation state
        if (!cancellation_id.empty()) {
            impl_->cancellation_registry_->unregister(cancellation_id);
            impl_->current_token_ = std::stop_token{};
            impl_->current_cancellation_id_.clear();
        }

        result.response = loop_result.value("response", "");
        result.total_steps = loop_result.value("steps", 0);
        result.total_tokens = loop_result.value("tokens_used", 0);
        result.cost_usd = loop_result.value("cost_usd", 0.0);
        result.success = true;

        if (result.success) {
            // 4. 追加 assistant 消息到历史
            nlohmann::json assistant_msg = {
                {"role", "assistant"},
                {"content", result.response},
                {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()},
                {"steps", result.total_steps},
                {"tokens", result.total_tokens}
            };
            impl_->messages.push_back(assistant_msg);

            // 5. emit "loop.done" (ADR-0068 §4 — args=业务字段, meta=trace context)
            impl_->bus->emit(agenticdsl::EventBuilder("loop.done")
                .args(nlohmann::json{
                    {"response", result.response},
                    {"total_steps", result.total_steps},
                    {"total_tokens", result.total_tokens}
                })
                .meta(nlohmann::json{{"session_id", session_id_}})
                .build());

            // T1 Budget 告警: 每轮后轮询 engine budget controller
            if (impl_->engine) {
                const auto& bc = impl_->engine->get_budget_controller();
                if (bc.exceeded()) {
                    double used = bc.get_total_cost_usd();
                    double limit = impl_->agent_cfg.budget_limit_usd;
                    impl_->bus->emit(agenticdsl::EventBuilder("budget.checked")
                        .args(nlohmann::json{
                            {"limit", limit},
                            {"used", used},
                            {"unit", "llm_calls"},
                            {"reason", "cost_limit"},
                            {"ok", false}
                        })
                        .meta(nlohmann::json{{"session_id", session_id_}})
                        .build());
                    result.success = false;
                    result.error_message = "[budget] cost_limit exceeded (used="
                        + std::to_string(used) + ", limit="
                        + std::to_string(limit) + ")";
                }
            }

            // 6. 持久化 (异步, 简化版 fire-and-forget)
            if (impl_->session_cfg.persist_dir != "") {
                impl_->bus->emit(agenticdsl::EventBuilder("session.persist_request")
                    .args(nlohmann::json{{"messages", nlohmann::json(impl_->messages)}})
                    .meta(nlohmann::json{{"session_id", session_id_}})
                    .build());
                // T1: 同步落盘 (原子写入) - 确保跨进程可恢复
                save_to_disk();
            }
        }
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
        impl_->bus->emit(agenticdsl::EventBuilder("loop.error")
            .args(nlohmann::json{{"error", result.error_message}})
            .meta(nlohmann::json{{"session_id", session_id_}})
            .build());
    }

    return result;
}

std::vector<nlohmann::json> ChatSession::history() const {
    return impl_->messages;
}

// === T1: Session 持久化实现 ===

namespace {

std::filesystem::path session_file_path(const std::string& dir, const std::string& id) {
    return std::filesystem::path(dir) / (id + ".json");
}

}  // namespace

bool ChatSession::load_from_disk(const std::string& session_id) {
    namespace fs = std::filesystem;
    auto path = session_file_path(impl_->persist_dir_expanded, session_id);

    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return false;
    }

    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[session/load] cannot open: " << path << std::endl;
        return false;
    }

    nlohmann::json j;
    try {
        f >> j;
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "[session/load] invalid JSON: " << path << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[session/load] error: " << path << ": " << e.what() << std::endl;
        return false;
    }

    // schema 版本校验
    int sv = j.value("schema_version", 0);
    if (sv != kSessionSchemaVersion) {
        std::cerr << "[session/load] unsupported schema_version " << sv
                  << " (expected " << kSessionSchemaVersion << "): " << path << std::endl;
        return false;
    }

    // T1.7 provider_mode reconcile: 恢复 history 但用本次 provider
    session_id_ = j.value("session_id", session_id);
    impl_->messages.clear();
    if (j.contains("history") && j["history"].is_array()) {
        for (const auto& msg : j["history"]) {
            impl_->messages.push_back(msg);
        }
    }
    // provider_mode 不恢复 - 保持本次构造时的 provider
    return true;
}

bool ChatSession::save_to_disk() {
    namespace fs = std::filesystem;
    if (impl_->persist_dir_expanded.empty()) return false;

    if (!ensure_dir_0700(impl_->persist_dir_expanded)) return false;

    auto path = session_file_path(impl_->persist_dir_expanded, session_id_);
    auto tmp = path;
    tmp += ".tmp";

    nlohmann::json j;
    j["schema_version"] = kSessionSchemaVersion;
    j["session_id"] = session_id_;
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch().count();
    j["created_at"] = epoch;
    j["updated_at"] = epoch;
    j["provider_mode"] = impl_->provider_mode;
    j["budget"] = {
        {"total", impl_->agent_cfg.budget_limit_usd},
        {"used", impl_->engine ? impl_->engine->get_budget_controller().get_total_cost_usd() : 0.0}
    };
    j["history"] = nlohmann::json(impl_->messages);

    {
        std::ofstream f(tmp);
        if (!f.is_open()) {
            std::cerr << "[session/save] cannot write tmp: " << tmp << std::endl;
            return false;
        }
        f << j.dump(2);
    }

    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        std::cerr << "[session/save] rename failed: " << ec.message() << std::endl;
        std::error_code rm_ec;
        fs::remove(tmp, rm_ec);
        return false;
    }

    // ADR-0068 §4 — 仅在原子 rename 成功后 emit session.persisted
    if (impl_->bus) {
        impl_->bus->emit(agenticdsl::EventBuilder("session.persisted")
            .args(nlohmann::json{
                {"session_id", session_id_},
                {"path", path.string()}
            })
            .meta(nlohmann::json{{"session_id", session_id_}})
            .build());
    }
    return true;
}

std::vector<std::string> ChatSession::list_sessions(const std::string& persist_dir) {
    namespace fs = std::filesystem;
    std::vector<std::string> ids;
    auto dir = expand_home(persist_dir);
    if (dir.empty()) return ids;

    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return ids;

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto p = entry.path();
        if (p.extension() == ".json") {
            ids.push_back(p.stem().string());
        }
    }
    return ids;
}

void ChatSession::cleanup_stale(const std::string& persist_dir, long long max_age_seconds) {
    namespace fs = std::filesystem;
    auto dir = expand_home(persist_dir);
    if (dir.empty()) return;

    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;

    auto now = fs::file_time_type::clock::now();
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;

        auto lwt = entry.last_write_time(ec);
        if (ec) {
            std::cerr << "[session/cleanup] stat failed: " << entry.path() << std::endl;
            continue;
        }
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - lwt).count();
        if (age > max_age_seconds) {
            std::error_code rm_ec;
            fs::remove(entry.path(), rm_ec);
            if (rm_ec) {
                std::cerr << "[session/cleanup] remove failed: " << entry.path()
                          << ": " << rm_ec.message() << std::endl;
            }
        }
    }
}

bool ChatSession::consume_budget_alert() {
    return budget_alert_flag_.exchange(false, std::memory_order_acq_rel);
}

// === Queue infrastructure (Phase A) ===

size_t ChatSession::queue_size(QueueKind kind) const {
    std::lock_guard<std::mutex> lock(
        kind == QueueKind::Steering ? impl_->steering_mutex_ : impl_->follow_up_mutex_);
    return (kind == QueueKind::Steering ? impl_->steering_queue_ : impl_->follow_up_queue_).size();
}

bool ChatSession::try_push_steering_for_test(const std::string& msg) {
    std::lock_guard<std::mutex> lock(impl_->steering_mutex_);
    if (impl_->steering_queue_.size() >= impl_->capacity_) {
        std::cerr << "[chat] steering queue overflow, rejected length=" << msg.size() << std::endl;
        return false;
    }
    impl_->steering_queue_.push(msg);
    // §2.5 NC2 fix: increment count atomically so pop_next_input predicate wakes
    impl_->pending_input_count_.fetch_add(1, std::memory_order_release);
    impl_->input_cv_.notify_one();
    return true;
}

bool ChatSession::try_push_follow_up_for_test(const std::string& msg) {
    std::lock_guard<std::mutex> lock(impl_->follow_up_mutex_);
    if (impl_->follow_up_queue_.size() >= impl_->capacity_) {
        std::cerr << "[chat] follow_up queue overflow, rejected length=" << msg.size() << std::endl;
        return false;
    }
    impl_->follow_up_queue_.push(msg);
    // §2.6 NC2 fix: increment count atomically so pop_next_input predicate wakes
    impl_->pending_input_count_.fetch_add(1, std::memory_order_release);
    impl_->input_cv_.notify_one();
    return true;
}

size_t ChatSession::try_clear_queue(QueueKind kind) {
    std::lock_guard<std::mutex> lock(
        kind == QueueKind::Steering ? impl_->steering_mutex_ : impl_->follow_up_mutex_);
    auto& q = (kind == QueueKind::Steering ? impl_->steering_queue_ : impl_->follow_up_queue_);
    size_t count = q.size();
    while (!q.empty()) q.pop();
    // §2.7 NC2 fix: fetch_sub(N) — preserves count invariant for the other queue
    impl_->pending_input_count_.fetch_sub(count, std::memory_order_acq_rel);
    return count;
}

std::optional<InputMessage> ChatSession::try_pop_input() {
    // §2.1 priority: steering before follow-up
    {
        std::lock_guard<std::mutex> lock(impl_->steering_mutex_);
        if (!impl_->steering_queue_.empty()) {
            InputMessage msg{QueueKind::Steering, impl_->steering_queue_.front()};
            impl_->steering_queue_.pop();
            impl_->pending_input_count_.fetch_sub(1, std::memory_order_acq_rel);
            return msg;
        }
    }
    {
        std::lock_guard<std::mutex> lock(impl_->follow_up_mutex_);
        if (!impl_->follow_up_queue_.empty()) {
            InputMessage msg{QueueKind::FollowUp, impl_->follow_up_queue_.front()};
            impl_->follow_up_queue_.pop();
            impl_->pending_input_count_.fetch_sub(1, std::memory_order_acq_rel);
            return msg;
        }
    }
    return std::nullopt;
}

std::optional<InputMessage> ChatSession::pop_next_input(std::chrono::milliseconds timeout) {
    // §2.2 fast-path: skip wait if already pending (avoids spurious wakeup overhead)
    if (impl_->pending_input_count_.load(std::memory_order_acquire) > 0) {
        if (auto msg = try_pop_input()) {
            return msg;
        }
    }
    std::unique_lock<std::mutex> lock(impl_->input_cv_mutex_);
    // §2.2/§2.4 predicate uses stop_input_thread_ as shutdown signal + count, NOT queue.empty()
    bool woken = impl_->input_cv_.wait_for(lock, timeout, [this]() {
        return impl_->stop_input_thread_.load(std::memory_order_acquire) ||
               impl_->pending_input_count_.load(std::memory_order_acquire) > 0;
    });
    if (!woken) return std::nullopt;  // timeout
    if (impl_->stop_input_thread_.load(std::memory_order_acquire) &&
        impl_->pending_input_count_.load(std::memory_order_acquire) == 0) {
        return std::nullopt;  // shutdown with empty queues
    }
    return try_pop_input();
}

std::optional<InputMessage> ChatSession::try_peek_input() const {
    // §2.8 NH1 fix: peek only, never consume, never modify count
    {
        std::lock_guard<std::mutex> lock(impl_->steering_mutex_);
        if (!impl_->steering_queue_.empty()) {
            return InputMessage{QueueKind::Steering, impl_->steering_queue_.front()};
        }
    }
    {
        std::lock_guard<std::mutex> lock(impl_->follow_up_mutex_);
        if (!impl_->follow_up_queue_.empty()) {
            return InputMessage{QueueKind::FollowUp, impl_->follow_up_queue_.front()};
        }
    }
    return std::nullopt;
}

bool ChatSession::is_input_thread_shutdown() const {
    return impl_->stop_input_thread_.load(std::memory_order_acquire);
}

void ChatSession::Impl::input_thread_main() {
    // Sprint 30 (D2 + D5): register periodic timer (50ms) for shutdown responsiveness.
    // D9 fallback: if timer_==nullptr, create per-thread TimerService (D9 lazy).
    // RAII guard cancels timer on all exit paths (EOF break / catch / normal return).
    if (timer_ != nullptr) {
        periodic_id_ = timer_->register_periodic(
            std::chrono::milliseconds(50),
            [this] { shutdown_check_pending_.store(true, std::memory_order_release); });
    }
    struct TimerGuard {
        agenticdsl::ITimerService* timer;
        agenticdsl::ITimerService::TimerId* id_ptr;
        ~TimerGuard() {
            if (timer && *id_ptr != 0) {
                timer->cancel(*id_ptr);
                *id_ptr = 0;
            }
        }
    } timer_guard{timer_, &periodic_id_};

    std::string line;
    while (!stop_input_thread_.load(std::memory_order_acquire)) {
        // Sprint 30 (D3): 周期性 timer callback 已 set shutdown_check_pending_,
        // 主循环 acquire-load 检查 (no-op in getline 阻塞场景, 但建立 pattern 供
        // Sprint 31+ chat-session-read-timeout poll/read 管道化使用)
        (void)shutdown_check_pending_.load(std::memory_order_acquire);

        if (!std::getline(std::cin, line)) {
            // §3.4 NH2 fix: EOF must signal shutdown AND wake any blocked pop_next_input
            stop_input_thread_.store(true, std::memory_order_release);
            input_cv_.notify_all();
            break;
        }
        if (line.empty()) continue;

        if (line.front() == '/') {
            std::lock_guard<std::mutex> lock(steering_mutex_);
            if (steering_queue_.size() < capacity_) {
                steering_queue_.push(line);
                // §3.1 push + count + notify in same mutex scope (C3 ordering fix)
                pending_input_count_.fetch_add(1, std::memory_order_release);
            } else {
                // §3.3 overflow: do NOT increment count, do NOT notify (no spurious wakeup)
                std::cerr << "[chat] steering queue overflow, rejected length=" << line.size() << std::endl;
                continue;
            }
        } else {
            std::lock_guard<std::mutex> lock(follow_up_mutex_);
            if (follow_up_queue_.size() < capacity_) {
                follow_up_queue_.push(line);
                pending_input_count_.fetch_add(1, std::memory_order_release);
            } else {
                std::cerr << "[chat] follow_up queue overflow, rejected length=" << line.size() << std::endl;
                continue;
            }
        }
        // §3.1/§3.2 notify OUTSIDE mutex (avoids holding mutex during wake; reduces contention)
        input_cv_.notify_one();
    }
}

}  // namespace pdk_chat_demo