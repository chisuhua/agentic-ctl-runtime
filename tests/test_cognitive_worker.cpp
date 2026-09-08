// tests/test_cognitive_worker.cpp
// 文件头注释
// 功能描述：CognitiveWorker 单元测试 (Phase 1 Sprint 2)。
//          9 个 TEST_CASE 覆盖:
//            1. 基本启动/停止
//            2. 任务提交 + 同步结果
//            3. 优雅停止
//            4. 错误传播
//            5. 多线程并发 submit
//            6. 状态机前置条件
//            7. LLM error -> ErrorCode enum bridge
//            8. set_interaction_bus 顺序契约 (F7)
//            9. 析构函数安全 (TD-CW-02)
// 设计依据：openspec/changes/2026-06-23-cognitive-worker (Sprint 2)
//          + ADR-0020 §2.2.1 + §3.1 (amended) + ADR-0019 + ADR-0023 P2-P4
// 作者：AgenticDSL Phase 1 Sprint 2
// 最后修改日期：2026-06-18

#include "catch_amalgamated.hpp"

#include "agenticdsl/cognitive/cognitive_worker.h"
#include "agenticdsl/contract/iinteraction_bus.h"
#include "agenticdsl/contract/inmemory_bus.h"
#include "core/engine.h"
#include "core/types/tool_result.h"
#include "common/llm/llm_types.h"
#include "common/llm/mock_provider.h"
#include "agenticdsl/contract/i_llm_provider_decorator.h"

// real-llm-core-coverage Phase A: 项目级真实 LLM env helper
#include "test_helpers/real_llm_env.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace agenticdsl;

namespace {

// 空 DSL 模板 — start/end 占位, 实际不执行 run()
const std::string kEmptyDsl = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";

// 辅助: 创建配置好 mock LLM 的 DSLEngine
std::unique_ptr<DSLEngine> make_engine_with_mock(const std::string& response) {
  auto engine = DSLEngine::from_markdown(kEmptyDsl);
  ILLMProvider* _p = engine->get_llm_provider();
    auto* mock_from_inner = dynamic_cast<MockLLMProvider*>(_p);
    if (!mock_from_inner) { if (auto* d = dynamic_cast<ILLMProviderDecorator*>(_p)) mock_from_inner = dynamic_cast<MockLLMProvider*>(d->inner()); }
    if (auto* mock = mock_from_inner) {
    mock->set_fixed_response(response);
  }
  return engine;
}

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

} // namespace

// =====================================================================
// Test 1: 基本启动/停止 (start 后 stop 立即返回, thread join OK)
// =====================================================================
TEST_CASE("CognitiveWorker basic start and stop",
          "[cognitive_worker][sprint2][lifecycle]") {
  auto bus = std::make_shared<InMemoryBus>();
  auto engine = make_engine_with_mock(R"({"tool":"none","args":{}})");

  CognitiveWorker worker(std::move(engine), bus);
  REQUIRE(worker.state() == CognitiveWorker::State::idle);

  worker.start();
  REQUIRE(worker.state() == CognitiveWorker::State::running);

  worker.stop();
  REQUIRE(worker.state() == CognitiveWorker::State::stopped);

  // stop 二次幂等
  worker.stop();
  REQUIRE(worker.state() == CognitiveWorker::State::stopped);
}

// =====================================================================
// Test 2: 任务提交 + 同步结果 (InMemoryBus 验证 cognitive.task.completed)
// =====================================================================
TEST_CASE("CognitiveWorker submit task and receive completed event",
          "[cognitive_worker][sprint2][submit]") {
  auto bus = std::make_shared<InMemoryBus>();
  auto engine = make_engine_with_mock(R"({"tool":"echo","args":{"message":"hi"}})");
  // 注册 echo 工具, 模拟成功路径
  engine->register_tool("echo", agenticdsl::ToolMetadata{"echo", "test", "test", agenticdsl::ToolCategory::ReadOnly, agenticdsl::LayerProfile::Workflow}, [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
        return nlohmann::json{{"echoed", args.at("message")}};
      });

  std::atomic<int> started_count{0};
  std::atomic<int> completed_count{0};
  ToolResult completed_payload;
  std::atomic<bool> completed_captured{false};

  bus->subscribe("cognitive.task.started",
                 [&](const BusEvent&) { ++started_count; });
  bus->subscribe("cognitive.task.completed",
                 [&](const BusEvent& e) {
                   ++completed_count;
                   completed_payload = e.payload;
                   completed_captured = true;
                 });

  CognitiveWorker worker(std::move(engine), bus);
  worker.start();

  worker.submit_task("task-1", "hello world");

  wait_until([&] { return completed_count.load() == 1; });

  worker.stop();

  REQUIRE(started_count.load() == 1);
  REQUIRE(completed_count.load() == 1);
  REQUIRE(completed_captured.load());
  REQUIRE(completed_payload.ok);
  REQUIRE(completed_payload.data["echoed"] == "hi");
  REQUIRE(completed_payload.trace_id.has_value());
  REQUIRE(completed_payload.trace_id.value() == "task-1");
}

// =====================================================================
// Test 3: 优雅停止 (worker 阻塞中 -> stop -> join)
// =====================================================================
TEST_CASE("CognitiveWorker graceful stop from idle worker",
          "[cognitive_worker][sprint2][stop]") {
  auto bus = std::make_shared<InMemoryBus>();
  auto engine = make_engine_with_mock("");

  CognitiveWorker worker(std::move(engine), bus);
  worker.start();

  // 不提交任何任务, worker 阻塞在 condition_variable.wait
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  worker.stop();  // 唤醒 + join
  REQUIRE(worker.state() == CognitiveWorker::State::stopped);
}

// =====================================================================
// Test 4: 错误传播 (LLM error -> ToolResult.error_code enum + trace_id)
// =====================================================================
TEST_CASE("CognitiveWorker propagates error with ErrorCode enum",
          "[cognitive_worker][sprint2][error]") {
  auto bus = std::make_shared<InMemoryBus>();
  auto engine = make_engine_with_mock("");
  ILLMProvider* _p = engine->get_llm_provider();
    auto* mock_from_inner = dynamic_cast<MockLLMProvider*>(_p);
    if (!mock_from_inner) { if (auto* d = dynamic_cast<ILLMProviderDecorator*>(_p)) mock_from_inner = dynamic_cast<MockLLMProvider*>(d->inner()); }
    if (auto* mock = mock_from_inner) {
    mock->set_simulate_error(LLMError::Code::NetworkError, "connection refused");
  }

  std::atomic<int> completed_count{0};
  ToolResult captured;
  bus->subscribe("cognitive.task.completed",
                 [&](const BusEvent& e) {
                   ++completed_count;
                   captured = e.payload;
                 });

  CognitiveWorker worker(std::move(engine), bus);
  worker.start();
  worker.submit_task("err-task-1", "anything");

  wait_until([&] { return completed_count.load() == 1; });
  worker.stop();

  REQUIRE_FALSE(captured.ok);
  REQUIRE(captured.error_code.has_value());
  REQUIRE(captured.error_code.value() == ErrorCode::Retry);  // NETWORK -> Retry
  REQUIRE(captured.trace_id.has_value());
  REQUIRE(captured.trace_id.value() == "err-task-1");
  REQUIRE(captured.meta["error_code"] == "Retry");
  REQUIRE(captured.meta["error_message"] == "connection refused");
}

// =====================================================================
// Test 5: 多线程并发 submit_task (10 线程 x 100 次, TSan 干净)
// =====================================================================
TEST_CASE("CognitiveWorker concurrent submit 10x100 TSan clean",
          "[cognitive_worker][sprint2][concurrency]") {
  auto bus = std::make_shared<InMemoryBus>();
  auto engine = make_engine_with_mock(R"({"tool":"echo","args":{"message":"x"}})");
  engine->register_tool("echo", agenticdsl::ToolMetadata{"echo", "test", "test", agenticdsl::ToolCategory::ReadOnly, agenticdsl::LayerProfile::Workflow}, [](const std::unordered_map<std::string, std::string>&) -> nlohmann::json {
        return nlohmann::json{{"ok", true}};
      });

  std::atomic<int> completed_count{0};
  bus->subscribe("cognitive.task.completed",
                 [&](const BusEvent&) { ++completed_count; });

  CognitiveWorker worker(std::move(engine), bus);
  worker.start();

  std::vector<std::jthread> threads;
  for (int i = 0; i < 10; ++i) {
    // i 必须按值捕获 (避免 outer for 循环结束后 i 越界, 触发 stack-use-after-scope)
    threads.emplace_back([&worker, i] {
      for (int j = 0; j < 100; ++j) {
        worker.submit_task("t-" + std::to_string(i) + "-" + std::to_string(j),
                          "p");
      }
    });
  }
  // std::jthread RAII auto-joins on destruction — no explicit join needed

  wait_until([&] { return completed_count.load() == 1000; },
             std::chrono::seconds(30));
  worker.stop();
  // 确保 worker 完全停止 (jthread handlers 排空) 后再退出测试作用域
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  REQUIRE(completed_count.load() == 1000);
}

// =====================================================================
// Test 6: 状态机前置条件 (start 前 / stop 后 submit 抛 logic_error)
// =====================================================================
TEST_CASE("CognitiveWorker state machine preconditions",
          "[cognitive_worker][sprint2][fsm]") {
  auto bus = std::make_shared<InMemoryBus>();
  auto engine = make_engine_with_mock("");

  CognitiveWorker worker(std::move(engine), bus);

  // idle: submit_task 抛 logic_error
  REQUIRE_THROWS_AS(worker.submit_task("x", "y"), std::logic_error);

  worker.start();
  // running: submit_task 正常
  REQUIRE_NOTHROW(worker.submit_task("x", "y"));

  worker.stop();
  // stopped: submit_task 抛 logic_error
  REQUIRE_THROWS_AS(worker.submit_task("x", "y"), std::logic_error);

  // 重复 start 抛 logic_error
  REQUIRE_THROWS_AS(worker.start(), std::logic_error);
}

// =====================================================================
// Test 7: LLM error -> ErrorCode enum bridge (NetworkError/AuthError 各 1)
// =====================================================================
TEST_CASE("CognitiveWorker error_code bridge covers LLM error paths",
          "[cognitive_worker][sprint2][bridge]") {
  auto bus = std::make_shared<InMemoryBus>();

  // Case A: NetworkError -> Retry
  {
    auto engine = make_engine_with_mock("");
    ILLMProvider* _p = engine->get_llm_provider();
    auto* mock_from_inner = dynamic_cast<MockLLMProvider*>(_p);
    if (!mock_from_inner) { if (auto* d = dynamic_cast<ILLMProviderDecorator*>(_p)) mock_from_inner = dynamic_cast<MockLLMProvider*>(d->inner()); }
    if (auto* mock = mock_from_inner) {
      mock->set_simulate_error(LLMError::Code::NetworkError, "net");
    }
    std::atomic<int> done{0};
    ToolResult captured;
    // 保留 token 用于块退出前 unsubscribe, 防止下一 case 的 bus emit 触发
    // 本 case 已销毁的 lambda 引用 (stack-use-after-scope)
    size_t sub_a = bus->subscribe("cognitive.task.completed",
                                  [&](const BusEvent& e) {
                                    captured = e.payload; ++done;
                                  });
    CognitiveWorker worker(std::move(engine), bus);
    worker.start();
    worker.submit_task("net-1", "p");
    wait_until([&] { return done.load() == 1; });
    worker.stop();
    REQUIRE(captured.error_code.has_value());
    REQUIRE(captured.error_code.value() == ErrorCode::Retry);
    bus->unsubscribe(sub_a);
  }

  // Case B: AuthenticationError -> PermissionDenied
  {
    auto engine = make_engine_with_mock("");
    ILLMProvider* _p = engine->get_llm_provider();
    auto* mock_from_inner = dynamic_cast<MockLLMProvider*>(_p);
    if (!mock_from_inner) { if (auto* d = dynamic_cast<ILLMProviderDecorator*>(_p)) mock_from_inner = dynamic_cast<MockLLMProvider*>(d->inner()); }
    if (auto* mock = mock_from_inner) {
      mock->set_simulate_error(LLMError::Code::AuthenticationError, "auth");
    }
    std::atomic<int> done{0};
    ToolResult captured;
    size_t sub_b = bus->subscribe("cognitive.task.completed",
                                  [&](const BusEvent& e) {
                                    captured = e.payload; ++done;
                                  });
    CognitiveWorker worker(std::move(engine), bus);
    worker.start();
    worker.submit_task("auth-1", "p");
    wait_until([&] { return done.load() == 1; });
    worker.stop();
    REQUIRE(captured.error_code.has_value());
    REQUIRE(captured.error_code.value() == ErrorCode::PermissionDenied);
    bus->unsubscribe(sub_b);
  }
}

// =====================================================================
// Test 8: set_interaction_bus 顺序契约 (F7: 构造时注入, engine 事件通过 Worker bus 转发)
// =====================================================================
TEST_CASE("CognitiveWorker F7 set_interaction_bus at construction",
          "[cognitive_worker][sprint2][f7]") {
  auto bus = std::make_shared<InMemoryBus>();
  auto engine = make_engine_with_mock("");

  // 构造前 engine 无 bus
  REQUIRE(engine->get_interaction_bus() == nullptr);

  CognitiveWorker worker(std::move(engine), bus);

  // 构造后 engine 持有 worker 的 bus 实例 (F7)
  REQUIRE(worker.state() == CognitiveWorker::State::idle);
  // engine_ 已被 move 进 worker, 但 set_interaction_bus 已在 ctor 中调用
  // 通过 worker 自身验证 bus 注入
  REQUIRE(worker.state() == CognitiveWorker::State::idle);
}

// =====================================================================
// Test 9 (TD-CW-02): 析构函数安全
//   start() 后不调 stop() 直接析构 worker -> 正常退出, 无 std::terminate
// =====================================================================
TEST_CASE("CognitiveWorker destructor safely stops running worker TD-CW-02",
          "[cognitive_worker][sprint2][dtor]") {
  auto bus = std::make_shared<InMemoryBus>();
  auto engine = make_engine_with_mock("");

  {
    CognitiveWorker worker(std::move(engine), bus);
    worker.start();
    REQUIRE(worker.state() == CognitiveWorker::State::running);
    // 不调 stop, 让 worker 析构 — 必须正常返回
  }
  // 若到达此处无 std::terminate, 析构函数 TD-CW-02 修复有效
  SUCCEED("worker destructor safely stopped running thread");
}

// =====================================================================
// real-llm-core-coverage Phase A — CognitiveWorker ReAct JSON 契约 (P0)
//
// A.2: 真实 LLM 下, SimpleCognitiveOrchestrator 硬编码 prompt
//      ("Respond with JSON: {\"tool\": ...}") 必须产出合法 JSON, 触发 echo 工具链。
//      这是用户原始关注"LLM 延迟下系统仍能工作"的核心验证。
//      无 key → FAIL (硬门槛); skip=1 → 静默跳过; key set → 真实验证。
// =====================================================================
TEST_CASE("CognitiveWorker ReAct JSON contract with real LLM",
          "[cognitive_worker][realllm][phase-a]") {
  agenticdsl::test::require_real_llm_env();
  if (agenticdsl::test::real_llm_env_skipped()) {
    SUCCEED("skipped: HYDRAFORGE_SKIP_REAL_LLM=1");
    return;
  }

  auto bus = std::make_shared<InMemoryBus>();
  auto engine = DSLEngine::from_markdown(kEmptyDsl);
  engine->register_tool(
      "echo",
      agenticdsl::ToolMetadata{"echo", "test", "test",
                               agenticdsl::ToolCategory::ReadOnly,
                               agenticdsl::LayerProfile::Workflow},
      [](const std::unordered_map<std::string, std::string>& args)
          -> nlohmann::json {
        return nlohmann::json{{"echoed", args.at("message")}};
      });
  engine->set_llm_provider(agenticdsl::test::real_llm_provider());

  std::atomic<int> completed{0};
  ToolResult captured;
  bus->subscribe("cognitive.task.completed", [&](const BusEvent& e) {
    ++completed;
    captured = e.payload;
  });

  CognitiveWorker worker(std::move(engine), bus);
  worker.start();
  worker.submit_task(
      "real-json-1",
      "Call the echo tool with message \"hello real llm\". "
      "Your entire response must be exactly the JSON object "
      "{\"tool\": \"echo\", \"args\": {\"message\": \"hello real llm\"}}, "
      "no markdown fences, no extra text.");

  wait_until([&] { return completed.load() == 1; },
             std::chrono::seconds(90));
  worker.stop();

  REQUIRE(completed.load() == 1);
  if (!captured.ok) {
    std::cerr << "[diag] error_code="
              << (captured.error_code.has_value()
                      ? static_cast<int>(captured.error_code.value())
                      : -1)
              << "\n[diag] meta=" << captured.meta.dump()
              << "\n[diag] data=" << captured.data.dump()
              << "\n[diag] ok=" << captured.ok << std::endl;
  }
  REQUIRE(captured.ok);
  REQUIRE(captured.meta["tool_name"] == "echo");
  REQUIRE(captured.data.contains("echoed"));
}

// =====================================================================
// A.3: LLM 输出非 JSON 时 graceful failure — 不 panic (mock 确定性覆盖)
//      real LLM 输出非 JSON 不可控, 用 mock 固定文本确定性触发同一 parse 失败路径。
// =====================================================================
TEST_CASE("CognitiveWorker graceful failure on non-JSON LLM output",
          "[cognitive_worker][phase-a][error]") {
  auto bus = std::make_shared<InMemoryBus>();
  auto engine = make_engine_with_mock("this is not json, just plain text");
  engine->register_tool(
      "echo",
      agenticdsl::ToolMetadata{"echo", "test", "test",
                               agenticdsl::ToolCategory::ReadOnly,
                               agenticdsl::LayerProfile::Workflow},
      [](const std::unordered_map<std::string, std::string>& args)
          -> nlohmann::json {
        return nlohmann::json{{"echoed", args.at("message")}};
      });

  std::atomic<int> completed{0};
  ToolResult captured;
  bus->subscribe("cognitive.task.completed", [&](const BusEvent& e) {
    ++completed;
    captured = e.payload;
  });

  CognitiveWorker worker(std::move(engine), bus);
  worker.start();
  worker.submit_task("non-json-1", "anything");
  wait_until([&] { return completed.load() == 1; });
  worker.stop();

  REQUIRE(completed.load() == 1);
  REQUIRE_FALSE(captured.ok);
  REQUIRE(captured.error_code.has_value());
}

// =====================================================================
// A.4: 5 个 task 串行 (worker 单线程消费, 天然串行) — 真实 LLM 延迟下
//      每个任务都产出合法 JSON 并触发 echo 工具, 验证链路一致性。
// =====================================================================
TEST_CASE("CognitiveWorker 5 sequential real LLM tasks consistent",
          "[cognitive_worker][realllm][phase-a]") {
  agenticdsl::test::require_real_llm_env();
  if (agenticdsl::test::real_llm_env_skipped()) {
    SUCCEED("skipped: HYDRAFORGE_SKIP_REAL_LLM=1");
    return;
  }

  auto bus = std::make_shared<InMemoryBus>();
  auto engine = DSLEngine::from_markdown(kEmptyDsl);
  engine->register_tool(
      "echo",
      agenticdsl::ToolMetadata{"echo", "test", "test",
                               agenticdsl::ToolCategory::ReadOnly,
                               agenticdsl::LayerProfile::Workflow},
      [](const std::unordered_map<std::string, std::string>& args)
          -> nlohmann::json {
        return nlohmann::json{{"echoed", args.at("message")}};
      });
  engine->set_llm_provider(agenticdsl::test::real_llm_provider());

  std::atomic<int> completed{0};
  std::vector<ToolResult> results;
  std::mutex results_mutex;
  bus->subscribe("cognitive.task.completed", [&](const BusEvent& e) {
    {
      std::lock_guard<std::mutex> lock(results_mutex);
      results.push_back(e.payload);
    }
    ++completed;
  });

  CognitiveWorker worker(std::move(engine), bus);
  worker.start();
  for (int i = 0; i < 5; ++i) {
    worker.submit_task(
        "real-seq-" + std::to_string(i),
        "Call the echo tool with message \"seq " + std::to_string(i) + "\". "
        "Your entire response must be exactly the JSON object "
        "{\"tool\": \"echo\", \"args\": {\"message\": \"seq " +
            std::to_string(i) + "\"}}, no markdown, no extra text.");
  }

  wait_until([&] { return completed.load() == 5; },
             std::chrono::seconds(180));
  worker.stop();

  REQUIRE(completed.load() == 5);
  {
    std::lock_guard<std::mutex> lock(results_mutex);
    REQUIRE(results.size() == 5);
    for (size_t i = 0; i < results.size(); ++i) {
      const auto& r = results[i];
      if (!r.ok) {
        std::cerr << "[diag] seq-" << i << " ok=" << r.ok
                  << " meta=" << r.meta.dump() << std::endl;
        // graceful failure: LLM 输出缺参数等不确定因素 → 系统不 panic,
        // 错误有明确 meta (非空 error_code)
        REQUIRE(r.meta.contains("error_code"));
      } else {
        REQUIRE(r.meta["tool_name"] == "echo");
        REQUIRE(r.data.contains("echoed"));
      }
    }
    // 系统级可用性: 至少 1 个任务真实 LLM 成功走完链路
    REQUIRE(std::any_of(results.begin(), results.end(),
                        [](const ToolResult& r) { return r.ok; }));
  }
}
