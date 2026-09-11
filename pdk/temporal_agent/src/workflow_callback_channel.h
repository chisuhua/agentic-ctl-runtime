// pdk/temporal_agent/src/workflow_callback_channel.h
// 功能描述：Workflow -> Agent Signal 双向通信通道
//          使用 ITimerService (Sprint 28 microkernel 第 1 件) 替代原 std::thread busy-poll:
//          消除 200ms busy-poll, 改用 timer_->register_periodic(50ms, ...)
//          handler 异常被隔离 (不终止 timer 线程)。
// 设计依据：openspec/changes/pkgm-temporal-agent/tasks.md §7.2
//          + openspec/changes/2026-09-10-kernel-timer-service/design.md §D6
// 线程安全：handlers_ 受 mutex 保护; stop() 确保 timer cancel
// 作者：pkgm-temporal-agent Phase 2 → Sprint 28 microkernel migration
// 最后修改日期：2026-09-XX

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "temporal_client.h"

#include "agenticdsl/contract/timer_service.h"

namespace pdk_temporal_agent {

class WorkflowCallbackChannel {
 public:
  using SignalHandler = std::function<void(const nlohmann::json&)>;

  explicit WorkflowCallbackChannel(std::string workflow_id);
  ~WorkflowCallbackChannel();

  WorkflowCallbackChannel(const WorkflowCallbackChannel&) = delete;
  WorkflowCallbackChannel& operator=(const WorkflowCallbackChannel&) = delete;

  // 注册信号处理器 (可在 start_polling 前或运行中调用)
  void on_signal(const std::string& signal_name, SignalHandler handler);

  // 启动后台 long-poll (通过 ITimerService 注册 periodic callback)
  // @param timer nullptr 时内部创建 TimerService (默认 std::jthread 实现)
  //              非 nullptr 时使用注入的 timer (测试可注入 mock)
  void start_polling(std::shared_ptr<ITemporalBackend> backend,
                     agenticdsl::ITimerService* timer = nullptr);

  // 停止轮询 (cancel periodic timer)
  void stop();

  // 是否正在轮询
  bool is_running() const { return running_.load(std::memory_order_relaxed); }

 private:
  // 单次 poll: 拉取 signals + 分发 handlers
  // 异常隔离: handler 调用 try-catch + catch(...)
  void poll_once();

  std::string workflow_id_;
  std::shared_ptr<ITemporalBackend> backend_;
  std::unordered_map<std::string, SignalHandler> handlers_;
  std::mutex handlers_mu_;

  // Sprint 28 microkernel migration: std::thread → ITimerService
  // owned_timer_ 持有所有权 (unique_ptr, RAII 自动析构),
  // timer_ 是观察者指针 (由 owned_timer_ 或外部传入 timer 初始化)
  std::unique_ptr<agenticdsl::ITimerService> owned_timer_;
  agenticdsl::ITimerService* timer_{nullptr};
  agenticdsl::ITimerService::TimerId periodic_id_{0};

  std::atomic<bool> running_{false};

  static constexpr auto kPollInterval = std::chrono::milliseconds(50);
};

}  // namespace pdk_temporal_agent
