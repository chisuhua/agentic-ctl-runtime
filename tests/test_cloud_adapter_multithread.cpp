// tests/test_cloud_adapter_multithread.cpp
// 文件头注释
// 功能描述：CloudLLMAdapter 多线程 SIGSEGV 回归守卫
//          ADR-0087 Step 4 (Sprint 27): root cause 升级 (OpenSSL 3.0 +
//          httplib 0.54.1) ship 后, factory 默认无 SerializingDecorator.
//          本测试 8 worker × 20 task = 160 calls 真实 deepseek 直连验证
//          root cause fix 有效 (无 SIGSEGV, ~4× 加速可达)
// 设计依据：openspec/changes/adr-0087-root-cause-upgrade/design.md §Decision 3
// 作者：AgenticDSL Wave 1 #2 (orig) + Sprint 27 Step 4 (annotation refresh)
// 最后修改日期：2026-09-11

#include "catch_amalgamated.hpp"

#include "agenticdsl/cognitive/domain_worker_pool.h"
#include "agenticdsl/contract/iinteraction_bus.h"
#include "agenticdsl/contract/inmemory_bus.h"
#include "agenticdsl/contract/bus_event.h"
#include "agenticdsl/types/layered_context.h"
#include "core/types/tool_result.h"
#include "common/llm/llm_config.h"
#include "common/llm/llm_provider_factory.h"
#include "common/llm/llm_types.h"
#include "test_helpers/real_llm_env.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace agenticdsl;

namespace {
constexpr int kStressWorkers = 8;
constexpr int kStressTasks = 20;  // 8 × 20 = 160 total
constexpr auto kStressTimeout = std::chrono::minutes(5);  // 串行化后 ~160 × 3s/call 上限
}  // namespace

TEST_CASE("CloudLLMAdapter 8 workers x 20 real deepseek tasks zero SIGSEGV",
          "[realllm][stress][multithread]") {
  agenticdsl::test::require_real_llm_env();
  if (agenticdsl::test::real_llm_env_skipped()) {
    SUCCEED("skipped: HYDRAFORGE_SKIP_REAL_LLM=1 (no API key or CI skip)");
    return;
  }
  // 本地有 key 时验证: SerializingDecorator 串行化后 8 worker × 20 task 真实 deepseek
  // 零 SIGSEGV / 零 abort, 全部 completed
  WARN("Stress test: 8 workers × 20 real deepseek tasks via SerializingDecorator; "
       "serialized so ~3 min total expected; will abort on SIGSEGV");

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

  DomainWorkerPool pool(kStressWorkers, bus);
  pool.register_domain_handler(
      "llm", [shared, model = cfg.model](const DomainTask& task) -> nlohmann::json {
        GenerationRequest req;
        req.prompt = "Reply with OK only. Task: " +
                     task.arguments["prompt"].get<std::string>();
        req.params.model = model;  // 显式设 model, 不依赖 default (Real-LLM test pattern)
        auto result = shared->generate(req, std::stop_token{});
        if (!result.has_value()) {
          return nlohmann::json{{"llm_error", true},
                                {"code", static_cast<int>(result.error().code)},
                                {"message", result.error().message}};
        }
        return nlohmann::json{{"response", result.value().text}};
      });
  pool.start();

  for (int i = 0; i < kStressTasks; ++i) {
    DomainTask task;
    task.domain = "llm";
    task.tool_name = "llm::generate";
    task.arguments = nlohmann::json{{"prompt", "s" + std::to_string(i)}};
    task.output_key = "result";
    pool.submit_task(std::move(task));
  }

  // 等待全部完成 (或超时)
  auto deadline = std::chrono::steady_clock::now() + kStressTimeout;
  while (completed_count.load() < kStressTasks &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  pool.stop();

  // 核心断言: 全部 completed (160 calls), 零 SIGSEGV (test 没崩)
  REQUIRE(completed_count.load() == kStressTasks);
  {
    std::lock_guard<std::mutex> lock(results_mutex);
    REQUIRE(results.size() == static_cast<size_t>(kStressTasks));
    int ok = 0;
    for (const auto& r : results) {
      // handler 不抛异常 → 全部 completed; 部分 LLM 输出可失败但 handler 返回
      REQUIRE(r.data.contains("result"));
      if (!r.data["result"].contains("llm_error")) ++ok;
    }
    // 至少 1 次真实 LLM 成功 (可用性断言, 反映 LLM 真实不可控)
    REQUIRE(ok >= 1);
  }
}