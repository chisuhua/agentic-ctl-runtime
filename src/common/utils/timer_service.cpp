// src/common/utils/timer_service.cpp
// 功能描述: TimerService 默认实现 (cv + steady_clock + std::jthread)
//          满足 ITimerService 契约 (include/agenticdsl/contract/timer_service.h)
// 设计依据: openspec/changes/2026-09-10-kernel-timer-service/design.md §D2/D4
//          + ADR-0021 (PDK Design) §3.5 — contract 层工具,PDK 可注入
// 作者: HydraForge Solo Dev
// 最后修改日期: 2026-09-12
#include "agenticdsl/contract/timer_service.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace agenticdsl {

class TimerService : public ITimerService {
 public:
  TimerService() {
    worker_ = std::jthread([this](std::stop_token st) {
      worker_loop(st);
    });
  }

  ~TimerService() override {
    // 关键修复 (fix-timer-service-destructor-hang): std::jthread RAII 自动 request_stop +
    // join, 但 std::condition_variable cv_.wait/wait_until **不响应 stop_token** — 必须显式
    // notify 才能唤醒 worker (否则 cv_ 永久 blocked → join() 永久 hang → test hang 15s
    // → SIGTERM). notify_all 在 worker_ dtor 之前调,确保 worker 拿到锁后能立即看到
    // stop_requested=true 退出循环
    cv_.notify_all();
  }

  TimerId register_oneshot(std::chrono::milliseconds delay,
                              Callback cb) override {
    if (!cb) return 0;
    if (delay.count() <= 0) delay = std::chrono::milliseconds{1};
    return add_timer_(std::chrono::steady_clock::now() + delay,
                       std::chrono::milliseconds{0},
                       std::move(cb));
  }

  TimerId register_periodic(std::chrono::milliseconds period,
                               Callback cb) override {
    if (!cb || period.count() <= 0) return 0;
    return add_timer_(std::chrono::steady_clock::now() + period,
                       period,
                       std::move(cb));
  }

  bool cancel(TimerId id) override {
    if (id == 0) return false;
    std::lock_guard<std::mutex> lock(mtx_);
    return timers_.erase(id) > 0;
  }

 private:
  struct TimerEntry {
    std::chrono::steady_clock::time_point deadline;
    std::chrono::milliseconds period;
    Callback cb;
  };

  TimerId add_timer_(std::chrono::steady_clock::time_point deadline,
                       std::chrono::milliseconds period,
                       Callback cb) {
    std::lock_guard<std::mutex> lock(mtx_);
    TimerId id = next_id_.fetch_add(1, std::memory_order_relaxed);
    timers_.emplace(id, TimerEntry{deadline, period, std::move(cb)});
    cv_.notify_one();
    return id;
  }

  bool has_earlier_deadline_(std::chrono::steady_clock::time_point ref) const {
    for (const auto& [id, entry] : timers_) {
      (void)id;
      if (entry.deadline < ref) return true;
    }
    return false;
  }

  void worker_loop(std::stop_token st) {
    while (!st.stop_requested()) {
      std::unique_lock<std::mutex> lock(mtx_);

      if (timers_.empty()) {
        // predicate 检查 stop_requested: jthread 的 request_stop() 不通知 cv_,所以需要
        // predicate 自己检查 stop_token. ~TimerService() 显式调 cv_.notify_all() 唤醒 worker
        cv_.wait(lock, [&] { return st.stop_requested() || !timers_.empty(); });
        if (st.stop_requested()) break;
        continue;
      }

      auto next_deadline = std::chrono::steady_clock::time_point::max();
      for (const auto& [id, entry] : timers_) {
        (void)id;
        if (entry.deadline < next_deadline) next_deadline = entry.deadline;
      }

      cv_.wait_until(lock, next_deadline, [&] {
        return st.stop_requested() || has_earlier_deadline_(next_deadline);
      });

      if (st.stop_requested()) break;

      const auto now = std::chrono::steady_clock::now();
      std::vector<std::pair<TimerId, Callback>> to_fire;
      std::vector<TimerId> to_remove;

      for (auto& [id, entry] : timers_) {
        if (entry.deadline <= now) {
          to_fire.emplace_back(id, entry.cb);
          if (entry.period.count() == 0) {
            to_remove.push_back(id);
          } else {
            entry.deadline += entry.period;
          }
        }
      }
      for (TimerId id : to_remove) timers_.erase(id);

      lock.unlock();
      for (auto& [id, cb] : to_fire) {
        (void)id;
        try { cb(); } catch (...) { /* 异常隔离: worker 不死 */ }
      }
      lock.lock();
    }
  }

  std::atomic<TimerId> next_id_{1};
  mutable std::mutex mtx_;
  // condition_variable_any 而非 condition_variable: 让 cv_.wait(lock, pred) / cv_.wait_until
  // 在 std::stop_token 触发时自动 notify (注册 stop_callback). 修复 ~TimerService() 永久 hang
  // bug (commit 8979b20 / change fix-timer-service-destructor-hang): 旧 condition_variable 不
  // 感知 stop_token,request_stop() 不调 notify,worker 永久 blocked,join() 永久 hang
  std::condition_variable_any cv_;
  std::map<TimerId, TimerEntry> timers_;
  std::jthread worker_;
};

std::unique_ptr<ITimerService> make_default_timer_service() {
  return std::make_unique<TimerService>();
}

}  // namespace agenticdsl
