// tests/test_stream_runtime.cpp
// ADR-0072 D1 阶段 B (2026-09-04): IStreamHandle runtime semantics
// 5 类 TEST_CASE / 19 cases:
//   类别 A (6): IStreamHandle 契约 (pull/push/close/close-with-error/cancel/三态)
//   类别 B (2): BufferedStreamHandle (累积 + EOF)
//   类别 C (4): CallbackStreamHandle (push 回调 + pull + 满队列阻塞 + 析构 RAII)
//   类别 D (5): NodeExecutor set_stream_sink 注入 (default null / tool_call 切片 /
//               dsl_call 切片 / stream false-or-missing 同步路径 / sink null 警告)
//   类别 E (2): stop_token 传播 (100ms 内取消观察 + mid-push 完成再终止)
// 设计依据: openspec/changes/adr-0072-d1-stream-runtime-semantics/
#include "catch_amalgamated.hpp"

#include "agenticdsl/contract/i_stream_handle.h"  // IStreamHandle (Step 2 新增)
#include "common/runtime/stream_handle.h"         // BufferedStreamHandle / CallbackStreamHandle (Step 2 新增)

#include "core/types/context.h"
#include "core/types/node.h"
#include "modules/executor/node_executor.h"
#include "common/tools/registry.h"
#include "common/llm/llm_types.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <queue>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace agenticdsl;

namespace {

// ─────────────────────────────────────────────────────────────
// 类别 D/E helper: 带 echo 工具的 ToolRegistry + NodeExecutor
// ─────────────────────────────────────────────────────────────

ToolMetadata make_meta(const std::string& name) {
  return ToolMetadata{name, "test", "test", ToolCategory::ReadOnly,
                      LayerProfile::Workflow};
}

ToolRegistry make_registry_with_echo() {
  ToolRegistry registry;
  registry.register_tool("echo", make_meta("echo"),
      [](const nlohmann::json& args) -> nlohmann::json {
        return nlohmann::json{{"ok", true}, {"result", args.value("text", "")}};
      });
  return registry;
}

// Minimal local ILLMTool mock for dsl_call stream:true test
class StreamMockLLMTool : public ILLMTool {
 public:
  explicit StreamMockLLMTool(std::string name) : name_(std::move(name)) {}
  std::string name() const override { return name_; }
  bool is_available() const override { return true; }
  LLMResult generate(const std::string& prompt, const LLMParams& /*params*/) override {
    LLMResult r;
    r.success = true;
    r.text = "Mock response for: " + prompt;
    r.tokens_generated = static_cast<int>(prompt.size());
    return r;
  }
 private:
  std::string name_;
};

// 记录 push 回调收到的 chunks
struct ChunkRecorder {
  std::mutex mu;
  std::vector<std::string> chunks;
  void operator()(const std::string& c) {
    std::lock_guard<std::mutex> lk(mu);
    chunks.push_back(c);
  }
  std::vector<std::string> snapshot() {
    std::lock_guard<std::mutex> lk(mu);
    return chunks;
  }
};

// RAII helper: 临时重定向 std::cerr 到 std::ostringstream (替代 GoogleTest CaptureStderr)
class StderrCapture {
 public:
  StderrCapture() : old_buf_(std::cerr.rdbuf(stream_.rdbuf())) {}
  ~StderrCapture() { std::cerr.rdbuf(old_buf_); }
  std::string str() const { return stream_.str(); }
 private:
  std::ostringstream stream_;
  std::streambuf* old_buf_;
};

}  // namespace

// ═════════════════════════════════════════════════════════════
// 类别 A: IStreamHandle 契约 (6 cases)
// ═════════════════════════════════════════════════════════════

TEST_CASE("IStreamHandle: pull drains to EOF (契约)", "[stream][runtime][category-a]") {
  BufferedStreamHandle handle;
  handle.push("chunk-1");
  handle.push("chunk-2");
  handle.close();  // 正常 EOF

  REQUIRE_FALSE(handle.is_active());  // close 后立即 inactive
  REQUIRE(handle.error() == std::nullopt);  // nullopt = 正常 EOF

  auto first = handle.next(std::stop_token{});
  REQUIRE(first.has_value());
  REQUIRE(*first == "chunk-1");
  auto second = handle.next(std::stop_token{});
  REQUIRE(second.has_value());
  REQUIRE(*second == "chunk-2");
  // EOF 后 next() 一律 nullopt
  REQUIRE_FALSE(handle.next(std::stop_token{}).has_value());
  REQUIRE_FALSE(handle.next(std::stop_token{}).has_value());
}

TEST_CASE("IStreamHandle: push writes chunk (契约)", "[stream][runtime][category-a]") {
  BufferedStreamHandle handle;
  handle.push("hello");
  auto chunk = handle.next(std::stop_token{});
  REQUIRE(chunk.has_value());
  REQUIRE(*chunk == "hello");
}

TEST_CASE("IStreamHandle: close signals EOF (契约)", "[stream][runtime][category-a]") {
  BufferedStreamHandle handle;
  REQUIRE(handle.is_active());
  handle.push("data");
  handle.close();
  REQUIRE_FALSE(handle.is_active());
  REQUIRE(handle.error() == std::nullopt);
  // close 后 push 是 no-op (idempotent)
  handle.push("after-close");  // MUST NOT throw, no-op
  // drain 已 push 的 chunk, 然后 nullopt
  auto chunk = handle.next(std::stop_token{});
  REQUIRE(chunk.has_value());
  REQUIRE(*chunk == "data");
  REQUIRE_FALSE(handle.next(std::stop_token{}).has_value());
}

TEST_CASE("IStreamHandle: close with error sets error state (契约)", "[stream][runtime][category-a]") {
  BufferedStreamHandle handle;
  handle.push("partial");
  handle.close(LLMError{LLMError::Code::ServerError, "producer blew up"});
  REQUIRE_FALSE(handle.is_active());
  auto err = handle.error();
  REQUIRE(err.has_value());
  REQUIRE(err->code == LLMError::Code::ServerError);
  // 剩余已 push chunk 可先拉取, 但最终终止
  auto chunk = handle.next(std::stop_token{});
  REQUIRE(chunk.has_value());
  REQUIRE(*chunk == "partial");
  REQUIRE_FALSE(handle.next(std::stop_token{}).has_value());
}

TEST_CASE("IStreamHandle: cancel via stop_token (契约)", "[stream][runtime][category-a]") {
  BufferedStreamHandle handle;
  handle.push("buffered");
  std::stop_source src;
  src.request_stop();
  auto chunk = handle.next(src.get_token());
  // token 已停止 → next 立即返回 nullopt + error()=Cancelled + is_active()=false
  REQUIRE_FALSE(chunk.has_value());
  REQUIRE_FALSE(handle.is_active());
  auto err = handle.error();
  REQUIRE(err.has_value());
  REQUIRE(err->code == LLMError::Code::Cancelled);
}

TEST_CASE("IStreamHandle: three-state distinction EOF/Cancel/Error (契约)", "[stream][runtime][category-a]") {
  // EOF: 正常 close → error() nullopt
  {
    BufferedStreamHandle handle;
    handle.close();
    REQUIRE_FALSE(handle.is_active());
    REQUIRE(handle.error() == std::nullopt);
  }
  // Cancel: stop_token 触发 → error() = Cancelled
  {
    BufferedStreamHandle handle;
    handle.push("x");
    std::stop_source src;
    src.request_stop();
    REQUIRE_FALSE(handle.next(src.get_token()).has_value());
    auto err = handle.error();
    REQUIRE(err.has_value());
    REQUIRE(err->code == LLMError::Code::Cancelled);
  }
  // Producer Error: close(err) → error() = 对应 Code
  {
    BufferedStreamHandle handle;
    handle.close(LLMError{LLMError::Code::NetworkError, "net down"});
    auto err = handle.error();
    REQUIRE(err.has_value());
    REQUIRE(err->code == LLMError::Code::NetworkError);
  }
}

// ═════════════════════════════════════════════════════════════
// 类别 B: BufferedStreamHandle (2 cases)
// ═════════════════════════════════════════════════════════════

TEST_CASE("BufferedStreamHandle: pull accumulates chunks", "[stream][runtime][category-b]") {
  BufferedStreamHandle handle;
  handle.push("A");
  handle.push("B");
  handle.push("C");
  handle.close();

  REQUIRE(handle.next(std::stop_token{}) == std::optional<std::string>("A"));
  REQUIRE(handle.next(std::stop_token{}) == std::optional<std::string>("B"));
  REQUIRE(handle.next(std::stop_token{}) == std::optional<std::string>("C"));
  REQUIRE_FALSE(handle.next(std::stop_token{}).has_value());  // EOF
}

TEST_CASE("BufferedStreamHandle: close marks EOF", "[stream][runtime][category-b]") {
  BufferedStreamHandle handle;
  REQUIRE(handle.is_active());
  handle.push("solo");
  handle.close();

  REQUIRE_FALSE(handle.is_active());
  // push 在 close 后是 no-op (不重新激活)
  handle.push("ghost");
  REQUIRE_FALSE(handle.is_active());
  REQUIRE(handle.next(std::stop_token{}) == std::optional<std::string>("solo"));
  REQUIRE_FALSE(handle.next(std::stop_token{}).has_value());
}

// ═════════════════════════════════════════════════════════════
// 类别 C: CallbackStreamHandle (4 cases)
// ═════════════════════════════════════════════════════════════

TEST_CASE("CallbackStreamHandle: invokes callback on push", "[stream][runtime][category-c]") {
  ChunkRecorder recorder;
  CallbackStreamHandle handle([&recorder](const std::string& c) { recorder(c); });
  handle.push("alpha");
  handle.push("beta");
  handle.close();

  auto seen = recorder.snapshot();
  REQUIRE(seen.size() == 2);
  REQUIRE(seen[0] == "alpha");
  REQUIRE(seen[1] == "beta");
}

TEST_CASE("CallbackStreamHandle: pull drains to EOF", "[stream][runtime][category-c]") {
  ChunkRecorder recorder;
  CallbackStreamHandle handle([&recorder](const std::string& c) { recorder(c); });
  handle.push("one");
  handle.push("two");
  handle.close();

  REQUIRE(handle.next(std::stop_token{}) == std::optional<std::string>("one"));
  REQUIRE(handle.next(std::stop_token{}) == std::optional<std::string>("two"));
  REQUIRE_FALSE(handle.next(std::stop_token{}).has_value());
}

TEST_CASE("CallbackStreamHandle: full queue blocks push", "[stream][runtime][category-c]") {
  // capacity=32: push 33 个 chunk 到满队列后, 第 33 个 push 应阻塞直到 consumer 拉取
  CallbackStreamHandle handle(nullptr);  // 无回调, 纯队列
  for (int i = 0; i < 32; ++i) {
    handle.push("chunk-" + std::to_string(i));
  }

  std::atomic<bool> push_returned{false};
  std::thread producer([&]() {
    handle.push("overflow");  // 队列满 → 阻塞
    push_returned.store(true);
  });

  // 给 producer 一点时间进入阻塞
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  REQUIRE_FALSE(push_returned.load());  // 仍在阻塞

  // consumer 拉取 1 个 → 腾出空间 → producer 解除阻塞
  auto chunk = handle.next(std::stop_token{});
  REQUIRE(chunk.has_value());

  producer.join();
  REQUIRE(push_returned.load());  // push 已完成

  // 验证 overflow 在队列中 (最后被拉取)
  std::string last;
  while (auto c = handle.next(std::stop_token{})) {
    last = *c;
  }
  REQUIRE(last == "overflow");
}

TEST_CASE("CallbackStreamHandle: clears callback on destruct", "[stream][runtime][category-c]") {
  // 析构时 callback std::function 被清空 → 析构后不能有悬空调用
  // (验证方式: 在析构前 push 已触发; 析构后 handle 不可再访问, 用作用域验证无崩溃)
  std::atomic<int> callback_count{0};
  {
    CallbackStreamHandle handle([&](const std::string&) { callback_count.fetch_add(1); });
    handle.push("x");
    REQUIRE(callback_count.load() == 1);
  }  // ~CallbackStreamHandle: mark inactive → clear queue → clear callback → notify_all
  REQUIRE(callback_count.load() == 1);  // 析构后无额外回调
}

// ═════════════════════════════════════════════════════════════
// 类别 D: NodeExecutor set_stream_sink 注入 (5 cases)
// ═════════════════════════════════════════════════════════════

TEST_CASE("NodeExecutor: set_stream_sink default null", "[stream][runtime][category-d]") {
  ToolRegistry registry = make_registry_with_echo();
  NodeExecutor executor(registry, nullptr);
  // 默认 sink == nullptr → stream:true 节点走同步 + 警告 (见 sink-null case)
  ToolCallNode node("/main/t", "echo", {{"text", "hi"}}, {"result"});
  node.metadata["stream"] = true;

  Context ctx;
  // 无 sink → 不崩溃, 同步执行
  REQUIRE_NOTHROW(executor.execute_node(&node, ctx));
  REQUIRE(ctx.contains("result"));
}

TEST_CASE("NodeExecutor: tool_call stream:true slices ToolResult to sink", "[stream][runtime][category-d]") {
  ToolRegistry registry = make_registry_with_echo();
  NodeExecutor executor(registry, nullptr);
  BufferedStreamHandle sink;
  executor.set_stream_sink(&sink);

  ToolCallNode node("/main/t", "echo", {{"text", "hello-streaming-world"}}, {"result"});
  node.metadata["stream"] = true;

  Context ctx;
  Context result = executor.execute_node(&node, ctx);
  REQUIRE(result.contains("result"));  // 同步路径结果仍写入 context

  sink.close(std::nullopt);
  // 累积所有 chunk 并拼接
  std::string received;
  while (auto c = sink.next(std::stop_token{})) {
    received += *c;
  }
  REQUIRE(received.find("hello-streaming-world") != std::string::npos);
}

TEST_CASE("NodeExecutor: dsl_call stream:true slices produced value to sink", "[stream][runtime][category-d]") {
  ToolRegistry registry;
  auto mock_llm = std::make_unique<StreamMockLLMTool>("mock_llm");
  registry.register_llm_tool("mock_llm", std::move(mock_llm), LLMParams{});

  NodeExecutor executor(registry, nullptr);
  BufferedStreamHandle sink;
  executor.set_stream_sink(&sink);

  DSLNode node("/main/generate", "Tell me {{ name }}", "mock_llm", LLMParams{}, {"response"});
  node.metadata["stream"] = true;

  Context ctx;
  ctx["name"] = "World";
  Context result = executor.execute_node(&node, ctx);
  REQUIRE(result.contains("response"));
  REQUIRE(result["response"] == "Mock response for: Tell me World");

  sink.close(std::nullopt);
  std::string received;
  while (auto c = sink.next(std::stop_token{})) {
    received += *c;
  }
  REQUIRE(received.find("Mock response for: Tell me World") != std::string::npos);
}

TEST_CASE("NodeExecutor: stream false or missing keeps sync path", "[stream][runtime][category-d]") {
  ToolRegistry registry = make_registry_with_echo();
  NodeExecutor executor(registry, nullptr);
  BufferedStreamHandle sink;
  executor.set_stream_sink(&sink);

  // metadata 缺省 → 同步路径, sink 无交互
  ToolCallNode missing_node("/main/missing", "echo", {{"text", "plain"}}, {"result"});
  Context ctx1;
  Context r1 = executor.execute_node(&missing_node, ctx1);
  REQUIRE(r1.contains("result"));
  REQUIRE(r1["result"] == "plain");
  // sink 不应收到任何 chunk (close 后 next 无值)
  sink.close(std::nullopt);
  REQUIRE_FALSE(sink.next(std::stop_token{}).has_value());

  // metadata["stream"] == false → 同步路径
  BufferedStreamHandle sink2;
  executor.set_stream_sink(&sink2);
  ToolCallNode false_node("/main/false", "echo", {{"text", "no-stream"}}, {"result"});
  false_node.metadata["stream"] = false;
  Context ctx2;
  Context r2 = executor.execute_node(&false_node, ctx2);
  REQUIRE(r2.contains("result"));
  sink2.close(std::nullopt);
  REQUIRE_FALSE(sink2.next(std::stop_token{}).has_value());
}

TEST_CASE("NodeExecutor: stream sink null emits warning", "[stream][runtime][category-d]") {
  ToolRegistry registry = make_registry_with_echo();
  NodeExecutor executor(registry, nullptr);  // sink 保持 nullptr
  ToolCallNode node("/main/t", "echo", {{"text", "hi"}}, {"result"});
  node.metadata["stream"] = true;

  Context ctx;
  // 捕获 stderr 以验证 warning (不崩溃 + 同步路径行为一致)
  StderrCapture capture;
  Context result = executor.execute_node(&node, ctx);
  std::string captured = capture.str();

  REQUIRE(result.contains("result"));
  REQUIRE(captured.find("stream:true ignored: no sink registered") != std::string::npos);
}

// ═════════════════════════════════════════════════════════════
// 类别 E: stop_token 传播 (2 cases)
// ═════════════════════════════════════════════════════════════

TEST_CASE("stop_token: cancel observes in next within 100ms", "[stream][runtime][category-e]") {
  BufferedStreamHandle handle;
  handle.push("pending");
  // handle 保持 active (未 close), consumer 稍后拉取

  std::stop_source src;
  auto t0 = std::chrono::steady_clock::now();
  auto chunk = handle.next(src.get_token());  // token 已停止
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();

  REQUIRE_FALSE(chunk.has_value());
  REQUIRE(elapsed_ms < 100);  // within 100ms
  auto err = handle.error();
  REQUIRE(err.has_value());
  REQUIRE(err->code == LLMError::Code::Cancelled);
}

TEST_CASE("stop_token: cancel mid-push completes then terminates", "[stream][runtime][category-e]") {
  // 模拟 producer 正在 push chunk N (NodeExecutor 执行线程), consumer 同时触发 stop。
  // V1 语义: push 已发生无法撤回; 后续 next(token) 在入口观察 stop → nullopt + Cancelled。
  BufferedStreamHandle handle;
  handle.push("in-flight-chunk");

  std::stop_source src;
  src.request_stop();  // 在 consumer 拉取前取消

  auto chunk = handle.next(src.get_token());
  REQUIRE_FALSE(chunk.has_value());  // cancel 在 next() 入口观察, 已入队 chunk 不再返回
  auto err = handle.error();
  REQUIRE(err.has_value());
  REQUIRE(err->code == LLMError::Code::Cancelled);
  REQUIRE_FALSE(handle.is_active());
}
