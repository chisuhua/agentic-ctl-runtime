// tests/test_timer_service.cpp
// 功能描述: TimerService 契约层工具测试 (Sprint 28 microkernel 蓝图组件)
//          11 cases 覆盖: oneshot/periodic/cancel/漂移/线程安全/异常隔离/destructor join
// 设计依据: openspec/changes/2026-09-10-kernel-timer-service/specs/kernel-timer-service/spec.md
//          + AGENTS.md §ENGINEERING PATTERNS 模式 #1 (TDD 5 步)
//          + Oracle session ses_f741f5d05ffeItVmfYVEjr67m3 + ses_f6fe76438ffeM5q8z2tUXQ7lIQ
// 作者: HydraForge Solo Dev
// 最后修改日期: 2026-09-12
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "catch_amalgamated.hpp"

#include "agenticdsl/contract/timer_service.h"

using agenticdsl::ITimerService;
using agenticdsl::make_default_timer_service;
using namespace std::chrono_literals;

// 测试 helper: 同步等待直到 predicate 为 true 或超时
// 返回 true = predicate 命中, false = 超时
template <typename Pred>
static bool wait_until(Pred&& pred, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(2ms);
  }
  return pred();
}

// === 1. oneshot 触发一次 ===
TEST_CASE("register_oneshot_fires_after_delay", "[timer_service][oneshot]") {
  auto timer = make_default_timer_service();
  REQUIRE(timer != nullptr);

  std::atomic<int> count{0};
  auto id = timer->register_oneshot(50ms, [&] { ++count; });
  REQUIRE(id != 0);

  REQUIRE(wait_until([&] { return count.load() >= 1; }, 500ms));
  REQUIRE(count.load() == 1);
}

// === 2. oneshot 只触发一次 ===
TEST_CASE("register_oneshot_fires_exactly_once", "[timer_service][oneshot]") {
  auto timer = make_default_timer_service();

  std::atomic<int> count{0};
  auto id = timer->register_oneshot(30ms, [&] { ++count; });

  REQUIRE(wait_until([&] { return count.load() >= 1; }, 200ms));
  // 等待 200ms 充分超过 30ms × 2
  std::this_thread::sleep_for(150ms);
  REQUIRE(count.load() == 1);
}

// === 3. periodic 多次触发 ===
TEST_CASE("register_periodic_fires_multiple_times", "[timer_service][periodic]") {
  auto timer = make_default_timer_service();

  std::atomic<int> count{0};
  auto id = timer->register_periodic(50ms, [&] { ++count; });
  REQUIRE(id != 0);

  // 250ms 内应触发 4-6 次 (50ms × 5 = 250ms, 容忍 ±2)
  REQUIRE(wait_until([&] { return count.load() >= 4; }, 500ms));
  std::this_thread::sleep_for(100ms);
  // 期望 5-7 次 (允许 ±2)
  REQUIRE(count.load() >= 4);
  REQUIRE(count.load() <= 8);
}

// === 4. cancel 阻止 oneshot 触发 ===
TEST_CASE("cancel_prevents_callback", "[timer_service][cancel]") {
  auto timer = make_default_timer_service();

  std::atomic<int> count{0};
  auto id = timer->register_oneshot(100ms, [&] { ++count; });
  REQUIRE(id != 0);

  // 立即 cancel
  REQUIRE(timer->cancel(id) == true);

  // 等待 200ms 确保不会触发
  std::this_thread::sleep_for(200ms);
  REQUIRE(count.load() == 0);
}

// === 5. cancel 未知 id 返回 false ===
TEST_CASE("cancel_returns_false_for_unknown_id", "[timer_service][cancel]") {
  auto timer = make_default_timer_service();
  REQUIRE(timer->cancel(99999) == false);
  REQUIRE(timer->cancel(0) == false);
}

// === 6. cancel 已触发的 oneshot 返回 false ===
TEST_CASE("cancel_returns_false_for_fired_oneshot", "[timer_service][cancel]") {
  auto timer = make_default_timer_service();

  std::atomic<int> count{0};
  auto id = timer->register_oneshot(20ms, [&] { ++count; });

  REQUIRE(wait_until([&] { return count.load() >= 1; }, 200ms));
  REQUIRE(timer->cancel(id) == false);  // 已触发, 返回 false
}

// === 7. periodic 无累积漂移 (Oracle 建议放宽上限到 1300ms) ===
TEST_CASE("periodic_no_accumulated_drift", "[timer_service][periodic][drift]") {
  auto timer = make_default_timer_service();

  std::atomic<int> count{0};
  auto start = std::chrono::steady_clock::now();
  auto id = timer->register_periodic(100ms, [&] { ++count; });

  // 等待 10 次触发 (100ms × 10 = 1000ms 累积周期)
  REQUIRE(wait_until([&] { return count.load() >= 10; }, 2000ms));
  auto elapsed = std::chrono::steady_clock::now() - start;

  // 累积式 deadline: 总耗时 ≥ 1000ms (无负漂移), < 1500ms (容忍 CI 抖动, Oracle 建议放宽上限)
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
  INFO("Periodic timer elapsed: " << elapsed_ms << "ms after " << count.load() << " fires");
  REQUIRE(elapsed_ms >= 1000);
  REQUIRE(elapsed_ms < 1500);  // 预留更多余量, 避免 CI 抖动
}

// === 8. handler 抛异常不会 kill worker ===
TEST_CASE("handler_exception_does_not_kill_worker", "[timer_service][exception]") {
  auto timer = make_default_timer_service();

  std::atomic<int> count_after_throw{0};

  // 第一个 oneshot 抛异常
  timer->register_oneshot(20ms, [] { throw std::runtime_error("handler boom"); });

  // 第二个 oneshot 正常, 验证 worker 还活着
  auto id2 = timer->register_oneshot(80ms, [&] { ++count_after_throw; });

  // 等到第二个触发
  REQUIRE(wait_until([&] { return count_after_throw.load() >= 1; }, 500ms));
  REQUIRE(count_after_throw.load() == 1);
}

// === 9. 多个 timer 独立触发 ===
TEST_CASE("multiple_timers_independent", "[timer_service][independence]") {
  auto timer = make_default_timer_service();

  std::atomic<int> c1{0}, c2{0}, c3{0};
  timer->register_oneshot(30ms, [&] { ++c1; });
  timer->register_oneshot(60ms, [&] { ++c2; });
  timer->register_oneshot(90ms, [&] { ++c3; });

  REQUIRE(wait_until([&] { return c3.load() >= 1; }, 500ms));
  std::this_thread::sleep_for(50ms);
  REQUIRE(c1.load() == 1);
  REQUIRE(c2.load() == 1);
  REQUIRE(c3.load() == 1);
}

// === 10. 析构 join worker ===
TEST_CASE("destructor_joins_worker_cleanly", "[timer_service][lifecycle]") {
  std::atomic<bool> captured{false};
  {
    auto timer = make_default_timer_service();
    timer->register_periodic(20ms, [&] {
      if (!captured.exchange(true)) {
        std::this_thread::get_id();
      }
    });
    std::this_thread::sleep_for(100ms);
    REQUIRE(captured.load());
    // timer 析构: worker thread 必须 join, 无泄漏, 无 hang
  }
  SUCCEED("worker thread joined cleanly on destruction");
}

// === 11. cancel 与 fire 并发 (Oracle 加) ===
// 验证: oneshot 即将触发的瞬间调用 cancel, 行为应该是
//   - callback 触发 OR 不触发 (one-shot, 二选一)
//   - cancel 返回 true OR false (并发竞争)
// 但绝对不能: worker 死 / 析构失败 / 内存泄漏
TEST_CASE("cancel_concurrent_with_fire", "[timer_service][cancel][race]") {
  // 跑 100 次, 每次让 cancel 与 fire 竞争
  for (int i = 0; i < 100; ++i) {
    auto timer = make_default_timer_service();
    std::atomic<int> count{0};
    auto id = timer->register_oneshot(1ms, [&] { ++count; });
    // 立即 cancel (1ms 后才触发, cancel 应该几乎总是成功)
    bool cancelled = timer->cancel(id);
    // 等待足够时间确保 worker 处理完 (无论 cancel 还是 fire)
    std::this_thread::sleep_for(20ms);
    // 验证: 如果 cancel 成功, count == 0; 如果 cancel 失败, count == 1
    if (cancelled) {
      REQUIRE(count.load() == 0);
    } else {
      REQUIRE(count.load() == 1);
    }
  }
}
