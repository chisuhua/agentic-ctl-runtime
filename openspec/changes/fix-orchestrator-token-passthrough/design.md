# fix-orchestrator-token-passthrough — Design

## 接口改动

### `include/agenticdsl/cognitive/simple_orchestrator.h`

```cpp
class SimpleCognitiveOrchestrator {
 public:
  // ...
  void process(const std::string& session_id,
               std::function<void(ToolResult)> on_complete,
               std::stop_token token = {});  // NEW: 默认 {} 保持向后兼容

 private:
  // ...
  ToolResult react_once(const std::string& user_prompt,
                       std::stop_token token = {});  // NEW: 默认 {} 保持向后兼容
};
```

### `src/modules/cognitive/simple_orchestrator.cpp`

```cpp
void SimpleCognitiveOrchestrator::process(
    const std::string& session_id,
    std::function<void(ToolResult)> on_complete,
    std::stop_token token) {
  // ... (原有异常捕获逻辑保留)
  ToolResult result = react_once(session_id, token);  // 透传
  // ...
}

ToolResult SimpleCognitiveOrchestrator::react_once(
    const std::string& user_prompt, std::stop_token token) {
  // ...
  GenerationRequest req;
  req.prompt = prompt + "\n[user] " + user_prompt;
  req.params.model.clear();
  auto result = llm_->generate(req, token);  // 修复: 替换硬编码 {}
  // ...
}
```

## 路径分析

```
外部 cancel token
  ↓
orch.process(s, cb, token)         [新形参]
  ↓
react_once(prompt, token)           [透传]
  ↓
llm_->generate(req, token)          [替换原 {}]
  ↓
CloudLLMAdapter::generate(req, token)  [Wave 1 #2 ship, 已 token-aware]
  ↓
CloudLLMAdapter 内部 httplib Post + Authorization header
  ↓ (cancellation 期间)
std::stop_requested() == true → return early
```

## 测试设计

### `tests/test_simple_orchestrator.cpp` Test 7 (新增)

```cpp
TEST_CASE("SimpleCognitiveOrchestrator forwards stop_token to LLM provider",
          "[cognitive][token][realllm-gap-fix]") {
  auto engine = DSLEngine::from_markdown(kEmptyDsl);
  // ... (register_tool echo)
  auto recorder = std::make_unique<RecordingLLMProvider>();
  recorder->result.text = R"({"tool":"echo","args":{"message":"ok"}})";
  auto* raw = recorder.get();
  engine->set_llm_provider(std::move(recorder));

  SimpleCognitiveOrchestrator orch(&engine->get_tool_registry(),
                                   engine->get_llm_provider());

  // pre-cancel: 验证 token 透传
  std::stop_source ss;
  ss.request_stop();
  bool done = false;
  ToolResult captured;
  orch.process("s7", [&](ToolResult r) {
    captured = std::move(r);
    done = true;
  }, ss.get_token());

  REQUIRE(done);
  REQUIRE(raw->generate_calls == 1);
  REQUIRE(raw->last_token_stop_requested == true);  // 核心契约
}
```

### RecordingLLMProvider 扩展 (additive)

```cpp
class RecordingLLMProvider : public ILLMProvider {
 public:
  std::string last_model;
  int generate_calls = 0;
  bool last_token_stop_requested = false;  // NEW
  GenerationResult result;

  Result<GenerationResult, LLMError> generate(
      const GenerationRequest& req, std::stop_token token) override {
    last_model = req.params.model;
    last_token_stop_requested = token.stop_requested();  // NEW
    ++generate_calls;
    return Result<GenerationResult, LLMError>::success(result);
  }
  // ...
};
```

## Oracle ship-gate 决策

待发起。预计 SHIP-with-fixes 或 SHIP（与 fix-yield-node-token-passthrough 模式相同）。

## 风险评估

- 生产风险：低（默认 `{}` 参数 + 既有硬编码 `{}` 等价）
- 测试风险：低（RecordingLLMProvider 新字段 additive，不影响其他 tests）
- API 兼容：100% 向后兼容（默认参数 + 既有 caller 零修改）
