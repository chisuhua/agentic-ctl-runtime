// pdk/temporal_agent/src/workflow_callback_channel.cpp
// 功能描述：WorkflowCallbackChannel 实现 - 通过 ITimerService periodic callback
//          替代原 std::thread + busy-poll (Sprint 28 microkernel 第 1 件迁移)
// 设计依据：openspec/changes/pkgm-temporal-agent/tasks.md §7.2
//          + openspec/changes/2026-09-10-kernel-timer-service/design.md §D6
// 线程安全：handlers_ 受 handlers_mu_ 保护; running_ 为 atomic
//          handler 调用异常 try/catch 隔离
// 作者：pkgm-temporal-agent Phase 2 → Sprint 28 microkernel migration
// 最后修改日期：2026-09-12

#include "workflow_callback_channel.h"

namespace pdk_temporal_agent {

WorkflowCallbackChannel::WorkflowCallbackChannel(std::string workflow_id)
    : workflow_id_(std::move(workflow_id)) {}

WorkflowCallbackChannel::~WorkflowCallbackChannel() {
  stop();
}

void WorkflowCallbackChannel::on_signal(const std::string& signal_name,
                                          SignalHandler handler) {
  std::lock_guard<std::mutex> lock(handlers_mu_);
  handlers_[signal_name] = std::move(handler);
}

void WorkflowCallbackChannel::start_polling(
    std::shared_ptr<ITemporalBackend> backend,
    agenticdsl::ITimerService* timer) {
  if (running_.load(std::memory_order_relaxed)) {
    return;
  }
  backend_ = std::move(backend);

  if (timer != nullptr) {
    timer_ = timer;
    owned_timer_.reset();
  } else {
    owned_timer_ = agenticdsl::make_default_timer_service();
    timer_ = owned_timer_.get();
  }

  running_.store(true, std::memory_order_relaxed);
  // periodic 累积 deadline 语义 (与 Linux timerfd_settime 一致):
  // 下次触发 = prev_deadline + period, 不是 now+period
  // 避免 handler 慢时周期漂移
  periodic_id_ = timer_->register_periodic(
      kPollInterval, [this] { poll_once(); });
}

void WorkflowCallbackChannel::stop() {
  if (!running_.exchange(false, std::memory_order_relaxed)) {
    return;
  }
  if (timer_ != nullptr && periodic_id_ != 0) {
    timer_->cancel(periodic_id_);
    periodic_id_ = 0;
  }
  timer_ = nullptr;
  owned_timer_.reset();
}

void WorkflowCallbackChannel::poll_once() {
  if (!backend_) {
    return;
  }

  auto signals = backend_->consume_signals(workflow_id_);
  for (const auto& sig : signals) {
    SignalHandler handler_copy;
    {
      std::lock_guard<std::mutex> lock(handlers_mu_);
      auto it = handlers_.find(sig.signal_name);
      if (it == handlers_.end()) {
        continue;
      }
      handler_copy = it->second;
    }
    // handler 在锁外调用 (避免死锁 + 异常隔离)
    // TimerService worker 内部已 try-catch + catch(...) 隔离, 这里再 double-guard
    try {
      handler_copy(sig.payload);
    } catch (...) {
      // 异常被吞掉, 后续 timer tick 继续
    }
  }
}

}  // namespace pdk_temporal_agent
