// chat_session.h - Chat Session 编排器
// 关联: docs/adr/adr-0060-agent-composition.md
//      docs/adr/adr-0033-session-hierarchy.md
//      openspec/changes/pdk-chat-demo-v1-recap/design.md (T1: 持久化 + Budget 告警)

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <stop_token>

#include <nlohmann/json.hpp>

#include "cancellation_registry.h"

namespace agenticdsl {
    class DSLEngine;
    class IToolRegistry;
    class IInteractionBus;
    class IBudgetController;
}

namespace pdk_chat_demo {

struct AgentConfig {
    std::string loop_type = "react";
    std::string provider = "mock";
    std::string model = "test";
    std::string system_prompt;
    std::vector<std::string> tools;
    int max_steps = 50;
    int timeout_ms = 300000;
    double budget_limit_usd = 1.0;
};

struct SessionConfig {
    std::string persist_dir = "~/.hydraforge/sessions/";
    int compact_threshold_tokens = 8000;
    bool branch_on_user_request = true;
    // ⚠️ 2026-09-09 默认值从 true 改为 false (fail-safe):
    // 默认 true 会导致所有用 SessionConfig{} 构造 ChatSession 的测试
    // 启动 stdin 读取线程 (input_thread_main → std::getline(std::cin)),
    // 在交互终端 (stdin=TTY) 下永久阻塞 → 测试死锁 → ctest TIMEOUT kill
    // (60s/120s 都救不了, 用户实测). 生产入口 main.cpp:447 已显式
    // enable_input_thread = true (single-reader mode), 不受影响.
    // 需要 stdin 的测试显式置 true (见 test_chat_session_queues.cpp).
    bool enable_input_thread = false;  // single-reader mode (chat-async-io-consumer-loop)
};

struct PluginConfig {
    std::string id;
    std::string path;
    std::string type = "so";      // so | skill | dsl | wasm
    std::string lifecycle = "eager";  // eager | lazy
    std::vector<std::string> activation_events;
    bool requires_isolation = false;
};

struct ObservabilityConfig {
    bool otel_enabled = false;
    std::string endpoint = "http://localhost:4318";
    double sample_rate = 1.0;
    std::string export_format = "otlp+http";
};

struct ChatConfig {
    std::string schema_version = "1.0";
    std::string app_id = "pdk_chat_demo";

    nlohmann::json providers;
    AgentConfig agent;
    std::vector<PluginConfig> plugins;
    nlohmann::json orchestration;
    ObservabilityConfig observability;
    SessionConfig session;
    nlohmann::json safety;

    // 从 JSON 文件加载
    static ChatConfig from_json(const std::string& path);

    // 切换到 mock provider (--mock flag)
    void override_provider(const std::string& provider, const std::string& model);

    void override_system_prompt(const std::string& overwrite,
                                const std::string& append);

    // 校验 manifest（schema 必填字段）
    void validate() const;
};

struct ChatResult {
    std::string response;
    int total_steps = 0;
    int total_tokens = 0;
    double cost_usd = 0.0;
    bool success = true;
    std::string error_message;
};

// ChatSession: 多轮对话编排器
// - 持有 UserSession (ADR-0033)
// - 每轮：emit user.input -> call_tool("loop/run") -> 收集 result
// - T1: 持久化 (load_from_disk/save_to_disk) + Budget 告警轮询
// QueueKind 标识 steering vs follow-up 队列
enum class QueueKind { Steering, FollowUp };

// InputMessage: 从队列取出的消息包装 (chat-async-io-consumer-loop §1.1)
struct InputMessage {
    QueueKind kind;
    std::string text;
};

class ChatSession {
public:
    ChatSession(
        agenticdsl::DSLEngine* engine,
        std::shared_ptr<agenticdsl::IInteractionBus> bus,
        agenticdsl::IToolRegistry* registry,
        const AgentConfig& agent_cfg,
        const SessionConfig& session_cfg,
        std::shared_ptr<CancellationRegistry> registry_arg = nullptr,  // §4.0.2/§4.0.9 shared registry (NC3 default)
        // Sprint 30 (chat-session-timer-migration) PIMPL void* handle:
        // 避开 chat_session.h 在 namespace pdk_chat_demo 内的 agenticdsl namespace pollution
        // (forward decl block 被 commands/*.cpp include 时嵌套为 pdk_chat_demo::agenticdsl,
        // 导致 agenticdsl::DSLEngine 等不可见)。 用 void* opaque handle:
        // 调用方传 static_cast<void*>(&timer), Impl 在 cpp 内 cast 回 ITimerService*。
        // - nullptr 默认 D9 lazy (Impl 不创建 jthread, input_thread 入口 fallback)
        // - 生命周期: 调用方保证 ChatSession 析构前 timer 无 in-flight callback (D10)
        // - Sprint 31+ Mode 修正: 移除 chat_session.h forward decl block + 各 commands/*.cpp
        //   改 include 完整 header, 可恢复 agenticdsl::ITimerService* 直接类型
        void* timer_handle = nullptr
    );

    ~ChatSession();

    // Request cancellation of any in-flight chat operation.
    void request_stop();

    // Wave 3-A Phase C: request model switch for next turn.
    // Returns true if accepted, false if rejected (e.g., mock mode + non-mock provider).
    bool request_model_switch(const std::string& provider_name);

    // Wave 3-A Phase C: get pending model switch target (empty = no pending switch).
    std::string next_model() const;

    // Existing API: now with optional stop_token
    ChatResult chat(const std::string& user_input);
    ChatResult chat(const std::string& user_input, std::stop_token token);

    const std::string& session_id() const { return session_id_; }

    std::vector<nlohmann::json> history() const;

    // === Queue infrastructure (Phase A: steering + follow-up bounded queues) ===
    // queue_size returns current entry count (thread-safe, O(1))
    size_t queue_size(QueueKind kind) const;

    // try_clear_queue atomically empties the queue, returns count cleared
    size_t try_clear_queue(QueueKind kind);

    // Test-only injection helpers (production code uses input thread)
    bool try_push_steering_for_test(const std::string& msg);
    bool try_push_follow_up_for_test(const std::string& msg);

    // === chat-async-io-consumer-loop §1.x consumer API ===
    // try_pop_input: non-blocking priority pop (steering > follow-up); returns nullopt if both empty
    std::optional<InputMessage> try_pop_input();

    // pop_next_input: blocking pop with timeout; returns nullopt on timeout OR shutdown
    std::optional<InputMessage> pop_next_input(std::chrono::milliseconds timeout);

    // try_peek_input: non-blocking peek at front (steering > follow-up); does NOT consume
    // (§NH1 fix: used by interrupt_thread to poll /cancel without losing the message)
    std::optional<InputMessage> try_peek_input() const;

    // §7.4 fix: distinguish timeout vs shutdown (true after EOF / signal)
    bool is_input_thread_shutdown() const;

    // === T1: Session 持久化 (design.md §Session 持久化) ===
    // 从磁盘加载 session (persist_dir/<id>.json)
    // 返回 true 表示成功; false 表示文件不存在/损坏/schema 版本不匹配
    // 损坏时打印 "[session/load] invalid JSON: <path>" 到 stderr 并返回空 session
    bool load_from_disk(const std::string& session_id);

    // 保存当前 session 到磁盘 (原子写入: tmp + rename)
    // 返回 true 表示成功
    bool save_to_disk();

    // 列出 persist_dir 下的所有 session_id (扫描 *.json)
    static std::vector<std::string> list_sessions(const std::string& persist_dir);

    // 清理 >24h 未活跃的 session 文件 (启动时调用)
    // 删除失败不抛异常, 打印警告到 stderr
    static void cleanup_stale(const std::string& persist_dir, long long max_age_seconds = 86400);

    // === T1: Budget 告警线程安全 (design.md §线程模型) ===
    // bus 回调置位此 flag; 主循环检查后渲染告警并重置
    std::atomic<bool> budget_alert_flag_{false};

    // 检查并消费 budget alert (主线程调用, 返回 true 表示有告警需渲染)
    bool consume_budget_alert();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::string session_id_;
};

}  // namespace pdk_chat_demo