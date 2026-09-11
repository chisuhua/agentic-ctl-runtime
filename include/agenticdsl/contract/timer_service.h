// include/agenticdsl/contract/timer_service.h
// 功能描述: ITimerService 通用定时器抽象契约 (Sprint 28 microkernel 蓝图组件)
//          解决项目 3 处分散的定时器实现:
//            1. WorkflowCallbackChannel::poll_loop — 200ms busy-poll
//            2. SkillInterpreter::Impl::ipc_loop_and_wait — 100ms poll
//            3. ChatSession::Impl::input_thread_main — std::getline 无超时
//          同 EventBuilder 先例 (ADR-0068): 放在 contract 层,PDK 可注入。
// 设计依据: openspec/changes/2026-09-10-kernel-timer-service/design.md
//          + ADR-0021 (PDK Design) §3.5 — contract 层工具,PDK 可注入
//          + ADR-0067 (Layered Plugin Architecture)
//          + ADR-0068 (EventBuilder 先例)
//          + Oracle session ses_f741f5d05ffeItVmfYVEjr67m3 评审通过
// 作者: HydraForge Solo Dev
// 最后修改日期: 2026-09-12
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace agenticdsl {

/**
 * @brief 通用定时器抽象契约 (contract 层)
 *
 * **线程模型**:
 *  - register_oneshot / register_periodic / cancel 不要求线程安全 (单线程注册契约, 同 DomainWorkerPool)
 *  - callback 在 TimerService 内部 worker 线程执行 (jthread + cv::wait_until)
 *  - callback 异常被 worker 内部 try-catch + catch(...) 隔离, 不会 kill worker
 *
 * **periodic 语义** (与 Linux timerfd_settime 一致):
 *  - 下次触发时间 = prev_deadline + period (累积式)
 *  - 若 handler 执行时间 > period, 可能追赶连发, 这是 fire-once 语义
 *  - 不要"修"成 now + period, 否则 handler 慢时周期漂移
 *
 * **取消语义**:
 *  - cancel(id) 返回 bool: true = 成功取消未触发的 timer
 *  - cancel 已触发或未知 id 返回 false (并发竞争: callback 正在执行也算已触发)
 *
 * **TimerId**:
 *  - uint64_t, 单调递增 (next_id_.fetch_add(1, relaxed))
 *  - 单进程 namespace, 不跨进程
 *
 * **实现**: TimerService (cv + steady_clock + jthread, ~150 LOC in src/common/utils/timer_service.cpp)
 */
class ITimerService {
 public:
  using TimerId = std::uint64_t;
  using Callback = std::function<void()>;

  virtual ~ITimerService() = default;

  /**
   * @brief 注册一次性 timer, delay 后触发 cb 一次
   * @param delay 延迟时长 (毫秒)
   * @param cb 回调函数 (无参数, 无返回值)
   * @return TimerId, 0 = invalid (注册失败)
   *
   * 异常: cb 为空 → 返回 0; delay == 0 → 等同于 delay = 1ms (避免竞态)
   */
  virtual TimerId register_oneshot(std::chrono::milliseconds delay,
                                    Callback cb) = 0;

  /**
   * @brief 注册周期性 timer, 每 period 触发 cb 一次
   * @param period 周期 (毫秒), 必须 > 0
   * @param cb 回调函数
   * @return TimerId, 0 = invalid
   *
   * 语义: prev_deadline + period 累积式 (与 timerfd 一致)
   */
  virtual TimerId register_periodic(std::chrono::milliseconds period,
                                     Callback cb) = 0;

  /**
   * @brief 取消 timer
   * @param id register_* 返回的 TimerId
   * @return true = 成功从 map 中移除 timer (返回前仍在锁内, 但**不等待已收集但尚未执行的 callback**)
   *         false = id 未知 / timer 已触发 (oneshot 已 fire) / id == 0
   *
   * 线程语义 (重要, 与 Oracle session `ses_f6f8ab1dbffeBh5kdvNi1SEE3k` SHIP-with-fixes 修正一致):
   * - cancel 与 callback **不互斥**: worker 在收集 fire 后 `lock.unlock()` 再执行 callback, 因此
   *   cancel(periodic_id) 返回 true 时, 上一次收集的 fire 可能仍在执行 (captures `[this]`).
   * - 销毁 TimerService / 持有方必须在 `~TimerService()` 后确保无 in-flight callback, 或主动等待
   *   worker join. 默认 owned TimerService (std::jthread RAII) 安全, **外部注入 timer 必须自行保证生命周期**.
   */
  virtual bool cancel(TimerId id) = 0;
};

/**
 * @brief 默认 TimerService 实现 (cv + steady_clock + jthread)
 *
 * 实现细节见 src/common/utils/timer_service.cpp
 * 工厂函数 make_default_timer_service() 返回 unique_ptr<ITimerService>
 */
class TimerService;

/**
 * @brief 工厂函数: 创建默认 TimerService 实例
 * @return unique_ptr<ITimerService> (TimerService 头文件唯一性 + 测试性)
 */
std::unique_ptr<ITimerService> make_default_timer_service();

}  // namespace agenticdsl

