// tests/test_domain_worker_pool.cpp
// 文件头注释
// 功能描述：DomainWorkerPool 单元测试 (Phase 1 Sprint 3)。
//          7 个 TEST_CASE 覆盖:
//            1. Pool 默认构造 (state == idle, num_threads 校验)
//            2. submit_task 派发到 worker (InMemoryBus 验证 domain.task.started/completed)
//            3. 1000x 并发 submit (10 thread × 100 task, 零 data race)
//            4. worker 异常隔离 (handler 抛 std::exception + 抛 int, worker 继续)
//            5. shutdown() 等待所有 in-flight task 完成
//            6. graceful_shutdown vs forced_shutdown (stop() 协作式 + 析构隐式 stop)
//            7. 与 IInteractionBus 集成 (subscribe domain.task.completed 验证 payload)
// 设计依据：openspec/changes/2026-06-30-domain-worker-pool (Sprint 3)
//          + ADR-0020 §2.2.1 P2 + §3.2 + ADR-0019 + ADR-0023 P1-P4
// 作者：AgenticDSL Phase 1 Sprint 3
// 最后修改日期：2026-06-19

#include "catch_amalgamated.hpp"

#include "agenticdsl/cognitive/domain_worker_pool.h"
#include "agenticdsl/contract/bus_event.h"
#include "agenticdsl/contract/iinteraction_bus.h"
#include "agenticdsl/contract/inmemory_bus.h"
#include "core/types/tool_result.h"
#include "common/llm/llm_types.h"
#include "common/llm/mock_provider.h"

// real-llm-core-coverage Phase B: 项目级真实 LLM env helper
#include "test_helpers/real_llm_env.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace agenticdsl;

namespace {

// 辅助: 等待条件谓词为 true, 超时 5s
template <typename Pred>
void wait_until(Pred&& pred,
                std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
  const auto start = std::chrono::steady_clock::now();
  while (!pred()) {
    if (std::chrono::steady_clock::now() - start > timeout) {
      FAIL("wait_until: timeout");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

// 辅助: 简单 echo handler — 把 arguments 包装为 {"echo": <args>}
auto make_echo_handler() {
  return [](const DomainTask& task) -> nlohmann::json {
    return nlohmann::json{{"echo", task.arguments}};
  };
}

// 辅助: 抛 std::exception 的 handler
auto make_throwing_handler() {
  return [](const DomainTask&) -> nlohmann::json {
    throw std::runtime_error("test handler exception");
  };
}

} // namespace

// =====================================================================
// Test 1: Pool 默认构造
// =====================================================================
TEST_CASE("DomainWorkerPool default construction",
          "[domain_worker_pool][sprint3][lifecycle]") {
  SECTION("default ctor (no bus)") {
    DomainWorkerPool pool(4);
    REQUIRE(pool.state() == DomainWorkerPool::State::idle);
    REQUIRE(pool.num_threads() == 4);

    // start 转换到 running
    pool.start();
    REQUIRE(pool.state() == DomainWorkerPool::State::running);

    // stop 转换到 stopped
    pool.stop();
    REQUIRE(pool.state() == DomainWorkerPool::State::stopped);

    // stop 二次幂等
    pool.stop();
    REQUIRE(pool.state() == DomainWorkerPool::State::stopped);
  }

  SECTION("ctor with bus") {
    auto bus = std::make_shared<InMemoryBus>();
    DomainWorkerPool pool(8, bus);
    REQUIRE(pool.state() == DomainWorkerPool::State::idle);
    REQUIRE(pool.num_threads() == 8);
  }

  SECTION("ctor with num_threads=0 throws") {
    REQUIRE_THROWS_AS(DomainWorkerPool(0), std::invalid_argument);
  }
}

// =====================================================================
// Test 2: submit_task 派发到 worker (InMemoryBus 验证)
// =====================================================================
TEST_CASE("DomainWorkerPool submit dispatches to worker",
          "[domain_worker_pool][sprint3][dispatch]") {
  auto bus = std::make_shared<InMemoryBus>();
  DomainWorkerPool pool(4, bus);

  // 订阅 domain.task.completed 事件
  std::atomic<int> completed_count{0};
  std::mutex completed_mutex;
  std::vector<ToolResult> completed_events;
  size_t token = bus->subscribe(
      "domain.task.completed",
      [&](const BusEvent& e) {
        std::lock_guard<std::mutex> lock(completed_mutex);
        completed_events.push_back(e.payload);
        completed_count.fetch_add(1, std::memory_order_relaxed);
      });

  pool.register_domain_handler("echo", make_echo_handler());
  pool.start();

  // 提交一个 task
  DomainTask task;
  task.domain = "echo";
  task.tool_name = "echo::test";
  task.arguments = nlohmann::json{{"message", "hello"}};
  task.output_key = "result";
  pool.submit_task(std::move(task));

  // 等待 completed 事件
  wait_until([&] { return completed_count.load() >= 1; });

  // 验证事件 payload
  {
    std::lock_guard<std::mutex> lock(completed_mutex);
    REQUIRE(completed_events.size() == 1);
    const auto& r = completed_events[0];
    REQUIRE(r.ok);
    REQUIRE(r.data.contains("result"));
    REQUIRE(r.data["result"]["echo"]["message"] == "hello");
    REQUIRE(r.meta["domain"] == "echo");
    REQUIRE(r.meta["tool_name"] == "echo::test");
    REQUIRE(r.meta["output_key"] == "result");
    REQUIRE(r.meta.contains("worker_id"));
  }

  pool.stop();
  bus->unsubscribe(token);
}

TEST_CASE("DomainWorkerPool submit_task(DomainTask.parent_trace) propagates to payload.parent_trace",
          "[domain_worker_pool][causal_ordering]") {
  auto bus = std::make_shared<InMemoryBus>();
  DomainWorkerPool pool(4, bus);

  std::mutex completed_mutex;
  std::vector<ToolResult> completed_events;
  bus->subscribe("domain.task.completed", [&](const BusEvent& e) {
    std::lock_guard<std::mutex> lock(completed_mutex);
    completed_events.push_back(e.payload);
  });

  pool.register_domain_handler("echo", make_echo_handler());
  pool.start();

  DomainTask task;
  task.domain = "echo";
  task.tool_name = "echo::test";
  task.arguments = nlohmann::json{{"message", "hello"}};
  task.output_key = "result";
  task.parent_trace = "task-P-1";
  pool.submit_task(std::move(task));

  wait_until([&] {
    std::lock_guard<std::mutex> lock(completed_mutex);
    return !completed_events.empty();
  });
  pool.stop();

  std::lock_guard<std::mutex> lock(completed_mutex);
  REQUIRE(completed_events.size() == 1);
  REQUIRE(completed_events[0].parent_trace.has_value());
  REQUIRE(*completed_events[0].parent_trace == "task-P-1");
}

// =====================================================================
// Test 3: 1000x 并发 submit (10 thread × 100 task, 零 data race)
// =====================================================================
TEST_CASE("DomainWorkerPool 1000x concurrent submit TSan clean",
          "[domain_worker_pool][sprint3][concurrent]") {
  DomainWorkerPool pool(4);
  std::atomic<int> handler_count{0};

  pool.register_domain_handler(
      "code", [&handler_count](const DomainTask& task) -> nlohmann::json {
        // 模拟 handler 工作 (sleep 1ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        handler_count.fetch_add(1, std::memory_order_relaxed);
        return nlohmann::json{{"id", task.arguments["id"]}};
      });

  pool.start();

  // 10 个线程各 submit 100 个 task
  constexpr int kNumThreads = 10;
  constexpr int kTasksPerThread = 100;
  std::vector<std::thread> submitter_threads;
  for (int t = 0; t < kNumThreads; ++t) {
    submitter_threads.emplace_back([&pool, t] {
      for (int i = 0; i < kTasksPerThread; ++i) {
        DomainTask task;
        task.domain = "code";
        task.tool_name = "code::noop";
        task.arguments = nlohmann::json{{"id", t * kTasksPerThread + i}};
        task.output_key = "out";
        pool.submit_task(std::move(task));
      }
    });
  }
  for (auto& t : submitter_threads) {
    t.join();
  }

  // 等待所有 task 处理完
  wait_until([&] {
    return handler_count.load() >= kNumThreads * kTasksPerThread;
  });

  REQUIRE(handler_count.load() == kNumThreads * kTasksPerThread);
  REQUIRE(pool.state() == DomainWorkerPool::State::running);

  pool.stop();
}

// =====================================================================
// Test 4: worker 异常隔离 (handler 抛 std::exception + 抛 int, worker 继续)
// =====================================================================
TEST_CASE("DomainWorkerPool worker exception isolation",
          "[domain_worker_pool][sprint3][exception]") {
  auto bus = std::make_shared<InMemoryBus>();
  DomainWorkerPool pool(2, bus);

  std::atomic<int> failed_count{0};
  std::atomic<int> completed_count{0};
  std::mutex events_mutex;
  std::vector<ToolResult> failed_events;

  bus->subscribe("domain.task.failed", [&](const BusEvent& e) {
    std::lock_guard<std::mutex> lock(events_mutex);
    failed_events.push_back(e.payload);
    failed_count.fetch_add(1, std::memory_order_relaxed);
  });
  bus->subscribe("domain.task.completed", [&](const BusEvent&) {
    completed_count.fetch_add(1, std::memory_order_relaxed);
  });

  pool.register_domain_handler("throwing", make_throwing_handler());
  pool.start();

  // 提交 10 个抛异常的 task
  for (int i = 0; i < 10; ++i) {
    DomainTask task;
    task.domain = "throwing";
    task.tool_name = "throwing::test";
    task.output_key = "out";
    pool.submit_task(std::move(task));
  }

  wait_until([&] { return failed_count.load() >= 10; });

  REQUIRE(failed_count.load() == 10);
  REQUIRE(completed_count.load() == 0);
  REQUIRE(pool.state() == DomainWorkerPool::State::running);  // worker 没死

  // 验证 failed 事件 payload
  {
    std::lock_guard<std::mutex> lock(events_mutex);
    for (const auto& r : failed_events) {
      REQUIRE_FALSE(r.ok);
      REQUIRE(r.error_code.has_value());
      REQUIRE(r.error_code.value() == ErrorCode::Unknown);
      REQUIRE(r.meta["error_message"].is_string());
    }
  }

  // 验证后续 task 仍可处理 (worker 继续)
  pool.register_domain_handler("echo", make_echo_handler());
  DomainTask ok_task;
  ok_task.domain = "echo";
  ok_task.tool_name = "echo::test";
  ok_task.output_key = "out";
  pool.submit_task(std::move(ok_task));

  wait_until([&] { return completed_count.load() >= 1; });
  REQUIRE(completed_count.load() == 1);

  pool.stop();
}

// =====================================================================
// Test 5: shutdown 等待所有 in-flight task 完成
// =====================================================================
TEST_CASE("DomainWorkerPool shutdown waits for in-flight tasks",
          "[domain_worker_pool][sprint3][shutdown]") {
  auto bus = std::make_shared<InMemoryBus>();
  DomainWorkerPool pool(4, bus);

  std::atomic<int> completed_count{0};
  bus->subscribe("domain.task.completed", [&](const BusEvent&) {
    completed_count.fetch_add(1, std::memory_order_relaxed);
  });

  pool.register_domain_handler("slow", [](const DomainTask&) -> nlohmann::json {
    // 模拟慢 handler (10ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return nlohmann::json{{"done", true}};
  });

  pool.start();

  // 提交 100 个 task
  for (int i = 0; i < 100; ++i) {
    DomainTask task;
    task.domain = "slow";
    task.tool_name = "slow::noop";
    task.output_key = "out";
    pool.submit_task(std::move(task));
  }

  // 立即 stop (in-flight task 必须完成, 无丢失)
  pool.stop();
  bus->wait_for_drain();

  REQUIRE(completed_count.load() == 100);
  REQUIRE(pool.state() == DomainWorkerPool::State::stopped);
}

// =====================================================================
// Test 6: graceful_shutdown vs forced_shutdown
// =====================================================================
TEST_CASE("DomainWorkerPool graceful vs forced shutdown",
          "[domain_worker_pool][sprint3][shutdown]") {
  SECTION("graceful shutdown via stop()") {
    DomainWorkerPool pool(4);
    pool.start();
    REQUIRE(pool.state() == DomainWorkerPool::State::running);

    pool.stop();
    REQUIRE(pool.state() == DomainWorkerPool::State::stopped);

    // stop 二次幂等
    pool.stop();
    REQUIRE(pool.state() == DomainWorkerPool::State::stopped);
  }

  SECTION("forced shutdown via destructor (no explicit stop)") {
    auto bus = std::make_shared<InMemoryBus>();
    std::atomic<int> completed_count{0};
    bus->subscribe("domain.task.completed", [&](const BusEvent&) {
      completed_count.fetch_add(1, std::memory_order_relaxed);
    });

    {
      DomainWorkerPool pool(2, bus);
      pool.register_domain_handler("echo", [](const DomainTask& t) -> nlohmann::json {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return nlohmann::json{{"echo", t.arguments}};
      });
      pool.start();

      // 提交 5 个 task
      for (int i = 0; i < 5; ++i) {
        DomainTask task;
        task.domain = "echo";
        task.tool_name = "echo::test";
        task.output_key = "out";
        pool.submit_task(std::move(task));
      }

      // 不调 stop(), 让 pool 析构 (强制关闭)
    }
    bus->wait_for_drain();

    // 析构后所有 in-flight task 仍应完成
    REQUIRE(completed_count.load() == 5);
  }

  SECTION("start on stopped pool throws") {
    DomainWorkerPool pool(2);
    pool.start();
    pool.stop();
    REQUIRE_THROWS_AS(pool.start(), std::logic_error);
  }

  SECTION("submit_task on stopped pool throws") {
    DomainWorkerPool pool(2);
    pool.start();
    pool.stop();

    DomainTask task;
    task.domain = "x";
    task.output_key = "out";
    REQUIRE_THROWS_AS(pool.submit_task(std::move(task)), std::logic_error);
  }
}

// =====================================================================
// Test 7: 与 IInteractionBus 集成 (subscribe domain.task.completed 验证 payload)
// =====================================================================
TEST_CASE("DomainWorkerPool bus integration",
          "[domain_worker_pool][sprint3][bus]") {
  auto bus = std::make_shared<InMemoryBus>();
  DomainWorkerPool pool(2, bus);

  std::atomic<int> started_count{0};
  std::atomic<int> completed_count{0};
  std::atomic<int> failed_count{0};
  // 使用 atomic flag 在 worker thread 中收集断言结果,在 main thread post-check
  // (避免 Catch2 REQUIRE 宏在 worker thread 调用导致 TSan data race,
  //  Catch2 framework 设计为单线程,worker thread 内调用 resetAssertionInfo
  //  会与 main thread 并发访问 framework 内部状态)
  std::atomic<bool> started_ok{false};
  std::atomic<bool> started_has_domain{false};
  std::atomic<bool> started_has_tool_name{false};
  std::atomic<bool> started_has_output_key{false};
  std::atomic<bool> started_has_worker_id{false};
  std::atomic<bool> failed_not_ok{false};
  std::atomic<bool> failed_has_error_message{false};
  std::mutex events_mutex;
  std::vector<ToolResult> completed_events;

  bus->subscribe("domain.task.started", [&](const BusEvent& e) {
    started_ok.store(e.payload.ok, std::memory_order_relaxed);
    // ADR-0068 §5.8: 业务字段在 data, trace 在 meta
    started_has_domain.store(e.payload.data.contains("domain"), std::memory_order_relaxed);
    started_has_tool_name.store(e.payload.data.contains("tool_name"), std::memory_order_relaxed);
    started_has_output_key.store(e.payload.data.contains("output_key"), std::memory_order_relaxed);
    started_has_worker_id.store(e.payload.meta.contains("worker_id"), std::memory_order_relaxed);
    started_count.fetch_add(1, std::memory_order_relaxed);
  });
  bus->subscribe("domain.task.completed", [&](const BusEvent& e) {
    std::lock_guard<std::mutex> lock(events_mutex);
    completed_events.push_back(e.payload);
    completed_count.fetch_add(1, std::memory_order_relaxed);
  });
  bus->subscribe("domain.task.failed", [&](const BusEvent& e) {
    failed_not_ok.store(!e.payload.ok, std::memory_order_relaxed);
    failed_has_error_message.store(e.payload.meta.contains("error_message"), std::memory_order_relaxed);
    failed_count.fetch_add(1, std::memory_order_relaxed);
  });

  pool.register_domain_handler("echo", [](const DomainTask& t) -> nlohmann::json {
    return nlohmann::json{{"echoed", t.arguments}, {"id", t.output_key}};
  });
  pool.start();

  SECTION("正常路径: domain.task.started + domain.task.completed") {
    DomainTask task;
    task.domain = "echo";
    task.tool_name = "echo::bus_test";
    task.arguments = nlohmann::json{{"msg", "hello"}};
    task.output_key = "result";
    pool.submit_task(std::move(task));

    wait_until([&] { return completed_count.load() >= 1; });

  REQUIRE(started_count.load() == 1);
  REQUIRE(completed_count.load() == 1);
  REQUIRE(failed_count.load() == 0);
  // post-check: worker thread 中收集的 atomic flag 在 main thread 验证
  // (避免 Catch2 REQUIRE 在 worker thread 调用导致 TSan data race)
  REQUIRE(started_ok.load());
  REQUIRE(started_has_domain.load());
  REQUIRE(started_has_tool_name.load());
  REQUIRE(started_has_output_key.load());
  REQUIRE(started_has_worker_id.load());

    std::lock_guard<std::mutex> lock(events_mutex);
    const auto& r = completed_events[0];
    REQUIRE(r.ok);
    REQUIRE(r.data.contains("result"));
    REQUIRE(r.data["result"]["echoed"]["msg"] == "hello");
    REQUIRE(r.data["result"]["id"] == "result");
  }

  SECTION("未注册 domain: domain.task.failed") {
    DomainTask task;
    task.domain = "unregistered";
    task.tool_name = "unregistered::test";
    task.output_key = "out";
    pool.submit_task(std::move(task));

    wait_until([&] { return failed_count.load() >= 1; });

  REQUIRE(started_count.load() == 1);
  REQUIRE(failed_count.load() == 1);
  REQUIRE(completed_count.load() == 0);
  // post-check: worker thread 中收集的 atomic flag 在 main thread 验证
  REQUIRE(failed_not_ok.load());
  REQUIRE(failed_has_error_message.load());
  }

  SECTION("重复注册抛异常") {
    REQUIRE_THROWS_AS(
        pool.register_domain_handler("echo", make_echo_handler()),
        std::invalid_argument);
  }

  SECTION("取消注册未注册的 domain 抛 out_of_range") {
    REQUIRE_THROWS_AS(
        pool.unregister_domain_handler("never_registered"),
        std::out_of_range);
  }

  pool.stop();
}

// =====================================================================
// real-llm-core-coverage Phase B — DomainWorkerPool 并发共享 provider (P0)
//
// B.2: N=4 worker 各自 submit task, 共享 1 个真实 deepseek provider 实例并发
//      generate — 验证线程安全 (cloud_adapter 每次新建 httplib::Client) +
//      handler 内显式设 params.model 规避默认值遮蔽 (Oracle P1-2 预判)。
//      无 key → FAIL; skip=1 → 静默跳过; key set → 真实验证。
// =====================================================================
TEST_CASE("DomainWorkerPool 4 workers concurrent real LLM via shared provider",
          "[domain_worker_pool][realllm][phase-b]") {
  agenticdsl::test::require_real_llm_env();
  if (agenticdsl::test::real_llm_env_skipped()) {
    SUCCEED("skipped: HYDRAFORGE_SKIP_REAL_LLM=1");
    return;
  }
  // Wave 1 #2 (fix-cloud-adapter-multithreading) 已 ship:
  // LLMProviderFactory::create cloud 路径注入 SerializingDecorator (mutex + cv
  // 串行化 generate), 根除 N≥2 worker 并发 + Authorization + https 的 SIGSEGV.
  // 本测试现恢复真实执行 (4 worker 共享 provider, 4 task 真实 deepseek).
  // 真根因修复 (OpenSSL 3.0 + httplib 升级) 留 ADR-0087 follow-up.
  auto cfg = agenticdsl::test::real_llm_config();
  auto provider = agenticdsl::test::real_llm_provider();
  ILLMProvider* shared = provider.get();

  auto bus = std::make_shared<InMemoryBus>();
  std::atomic<int> completed_count{0};
  std::mutex results_mutex;
  std::vector<ToolResult> results;
  bus->subscribe("domain.task.completed", [&](const BusEvent& e) {
    {
      std::lock_guard<std::mutex> lock(results_mutex);
      results.push_back(e.payload);
    }
    completed_count.fetch_add(1, std::memory_order_relaxed);
  });

  DomainWorkerPool pool(4, bus);
  pool.register_domain_handler(
      "llm", [shared, model = cfg.model](const DomainTask& task) -> nlohmann::json {
        GenerationRequest req;
        req.prompt = "Reply with the single word OK. Task: " +
                     task.arguments["prompt"].get<std::string>();
        req.params.model = model;  // 显式设 model — 规避 LLMConfig 默认遮蔽 (Oracle P1-2)
        auto result = shared->generate(req, std::stop_token{});
        if (!result.has_value()) {
          return nlohmann::json{{"llm_error", true},
                                {"code", static_cast<int>(result.error().code)},
                                {"message", result.error().message}};
        }
        return nlohmann::json{{"response", result.value().text}};
      });
  pool.start();

  for (int i = 0; i < 4; ++i) {
    DomainTask task;
    task.domain = "llm";
    task.tool_name = "llm::generate";
    task.arguments = nlohmann::json{{"prompt", "t" + std::to_string(i)}};
    task.output_key = "result";
    pool.submit_task(std::move(task));
  }

  wait_until([&] { return completed_count.load() >= 4; },
             std::chrono::seconds(120));
  pool.stop();

  REQUIRE(completed_count.load() == 4);
  {
    std::lock_guard<std::mutex> lock(results_mutex);
    REQUIRE(results.size() == 4);
    int ok = 0;
    for (const auto& r : results) {
      REQUIRE(r.ok);  // handler 不抛异常 → 全部 completed
      REQUIRE(r.data.contains("result"));
      if (!r.data["result"].contains("llm_error")) ++ok;
    }
    REQUIRE(ok >= 1);  // 至少 1 次真实 LLM 成功 (可用性, LLM 输出错误不 panic)
  }
}

// =====================================================================
// B.3: 1 worker × 1 共享 mock provider × 100 task 串行 — 验证共享 provider
//      状态在 100 次连续调用下稳定, 无 race 无状态污染 (零延迟确定性).
//      (真实 LLM 100 次串行需 5-15 分钟, 不合理; 并发真实安全性由 B.2 覆盖)
// =====================================================================
TEST_CASE("DomainWorkerPool 1 worker x 100 serial shared mock provider no race",
          "[domain_worker_pool][phase-b][concurrency]") {
  auto bus = std::make_shared<InMemoryBus>();
  auto provider = std::make_shared<MockLLMProvider>();
  provider->set_fixed_response(R"({"ok":true})");

  std::atomic<int> completed_count{0};
  bus->subscribe("domain.task.completed", [&](const BusEvent&) {
    completed_count.fetch_add(1, std::memory_order_relaxed);
  });

  DomainWorkerPool pool(1, bus);
  pool.register_domain_handler(
      "llm", [provider](const DomainTask& task) -> nlohmann::json {
        GenerationRequest req;
        req.prompt = "mock task " + std::to_string(task.arguments["id"].get<int>());
        auto result = provider->generate(req, std::stop_token{});
        if (!result.has_value()) {
          return nlohmann::json{{"llm_error", true}};
        }
        return nlohmann::json{{"response", result.value().text}};
      });
  pool.start();

  for (int i = 0; i < 100; ++i) {
    DomainTask task;
    task.domain = "llm";
    task.tool_name = "llm::generate";
    task.arguments = nlohmann::json{{"id", i}};
    task.output_key = "result";
    pool.submit_task(std::move(task));
  }

  wait_until([&] { return completed_count.load() >= 100; },
             std::chrono::seconds(30));
  pool.stop();
  REQUIRE(completed_count.load() == 100);
}

// =====================================================================
// B.4: RateLimited 优雅处理 + 无重试风暴 — 用 MockLLMProvider 模拟 429,
//      DomainWorkerPool handler 调共享 provider → 验证 error path 优雅传递,
//      全部 completed 事件 + 总耗时 < 阈值 (无 hang/重试风暴).
//      注: 原计划用 CloudLLMAdapter + HttpMockServer 测真实 429 HTTP path,
//      但实测当前环境下 CloudLLMAdapter + httplib::Client::Post 在并发/带
//      Authorization header 场景下 SIGSEGV (create_client_socket 崩溃) —
//      记录为跟进 change `fix-cloud-adapter-threading` (httplib/OpenSSL
//      多线程 init 与 Authorization header 处理的栈 corruption, 超出本
//      change scope). 本测试聚焦 DomainWorkerPool 自身的 error 传递路径.
// =====================================================================
TEST_CASE("DomainWorkerPool RateLimited handled gracefully no retry storm",
          "[domain_worker_pool][phase-b][ratelimit]") {
  auto provider = std::make_shared<MockLLMProvider>();
  provider->set_simulate_error(LLMError::Code::RateLimited, "rate limited");

  auto bus = std::make_shared<InMemoryBus>();
  std::atomic<int> completed_count{0};
  std::atomic<int> generate_calls{0};
  std::mutex results_mutex;
  std::vector<ToolResult> results;

  bus->subscribe("domain.task.completed", [&](const BusEvent& e) {
    {
      std::lock_guard<std::mutex> lock(results_mutex);
      results.push_back(e.payload);
    }
    completed_count.fetch_add(1, std::memory_order_relaxed);
  });

  DomainWorkerPool pool(4, bus);
  pool.register_domain_handler(
      "llm", [provider, &generate_calls](const DomainTask&) -> nlohmann::json {
        GenerationRequest req;
        req.prompt = "hello";
        req.params.model = "deepseek-v4-flash";
        generate_calls.fetch_add(1, std::memory_order_relaxed);
        auto result = provider->generate(req, std::stop_token{});
        if (!result.has_value()) {
          return nlohmann::json{{"llm_error", true},
                                {"code", static_cast<int>(result.error().code)},
                                {"retryable", result.error().retryable()}};
        }
        return nlohmann::json{{"response", result.value().text}};
      });
  pool.start();

  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < 4; ++i) {
    DomainTask task;
    task.domain = "llm";
    task.tool_name = "llm::generate";
    task.output_key = "result";
    pool.submit_task(std::move(task));
  }
  wait_until([&] { return completed_count.load() >= 4; },
             std::chrono::seconds(15));
  pool.stop();
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - t0)
          .count();

  REQUIRE(completed_count.load() == 4);
  REQUIRE(generate_calls.load() == 4);  // 无重试 (handler 每次 generate 一次)
  REQUIRE(elapsed < 30);                // 无 hang
  {
    std::lock_guard<std::mutex> lock(results_mutex);
    int rate_limited = 0;
    for (const auto& r : results) {
      REQUIRE(r.ok);  // handler 不抛 → 全部 completed
      REQUIRE(r.data["result"].contains("llm_error"));
      if (r.data["result"]["code"] == static_cast<int>(LLMError::Code::RateLimited)) {
        ++rate_limited;
      }
    }
    REQUIRE(rate_limited == 4);  // 全部传递 RateLimited 错误码
  }
}
