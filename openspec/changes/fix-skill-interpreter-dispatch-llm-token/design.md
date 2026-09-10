# fix-skill-interpreter-dispatch-llm-token — Design

## 接口改动

### `src/modules/skill_interpreter/skill_interpreter.cpp` (3 处)

```cpp
class SkillInterpreter::Impl {
  // ...
  SkillResult ipc_loop_and_wait(pid_t pid, int pipe_out_r, int pipe_in_w,
                                 int pipe_err_r, const SkillCapability& cap,
                                 std::stop_token token = {}) {
    // ...
    while (true) {
      // ...
      // 调用 dispatch 时透传 token:
      IPCResponse resp = dispatch(req, cap, pid, token);  // NEW: token 透传
      // ...
    }
  }

  IPCResponse dispatch(const IPCRequest& req, const SkillCapability& cap,
                       pid_t pid, std::stop_token token = {}) {  // NEW
    if (req.method == "call_tool") {
      return dispatch_call_tool(req, cap, pid);
    } else if (req.method == "emit_event") {
      return dispatch_emit_event(req, cap);
    } else if (req.method == "llm_generate") {
      return dispatch_llm_generate(req, cap, token);  // NEW: token 透传
    } else if (req.method == "consume_budget") {
      return dispatch_consume_budget(req, cap, pid);
    } else if (req.method == "return") {
      return IPCResponse{true, req.params.value("value", nlohmann::json::object())};
    } else {
      return IPCResponse{false, nullptr, "unknown method"};
    }
  }

  IPCResponse dispatch_llm_generate(const IPCRequest& req,
                                   const SkillCapability& cap,
                                   std::stop_token token = {}) {  // NEW
    if (!cap.allow_llm) {
      return IPCResponse{false, nullptr, "llm_generate not allowed"};
    }
    // ...
    try {
      GenerationRequest gen_req;
      gen_req.prompt = req.params.value("prompt", "");
      gen_req.params.model.clear();
      auto result = llm_->generate(gen_req, token);  // NEW: 替换硬编码 {}
      // ...
    }
  }
};
```

## 路径分析

```
外部 cancel token
  ↓
SkillInterpreter::run(skill, cap, token)              [Wave 4 #3 已加]
  ↓
Impl::run(skill, cap, token)                            [Wave 4 #3 已加]
  ↓
ipc_loop_and_wait(pid, ..., cap, token)                 [Wave 4 #3 已加]
  ↓
while(true) {
  poll() 返回 IPCRequest
  ↓
  dispatch(req, cap, pid, token)                       [本 change: 加 token 形参]
  ↓
  dispatch_llm_generate(req, cap, token)               [本 change: 加 token 形参]
  ↓
  llm_->generate(gen_req, token)                        [本 change: 替换硬编码 {}]
  ↓
  CloudLLMAdapter::generate(req, token) → 立即响应 stop_token
}
```

## 测试设计

### `tests/test_skill_interpreter.cpp` 新 Test 7.8d

```cpp
TEST_CASE("7.8d dispatch_llm_generate forwards external stop_token",
          "[skill_interpreter][token][llm_generate][realllm-followup]") {
    // 使用 RecordingLLMProvider 替代 MockToolRegistry (token-aware)
    auto recorder = std::make_unique<RecordingLLMProvider>();
    recorder->result.text = "ok";
    auto* raw = recorder.get();
    // 设置 interpreter 的 llm 指针为 recorder
    SkillInterpreter interpreter(tools, bus, raw, nullptr);

    std::string skill = create_temp_skill(
        "---\n"
        "name: llm-token-test\n"
        "version: 0.1\n"
        "description: test llm_generate token forwarding\n"
        "---\n"
        "llm_generate({\"prompt\": \"hi\"})\n");
    REQUIRE(!skill.empty());

    SkillCapability cap;
    cap.allow_llm = true;
    cap.max_steps = 10;
    cap.timeout_ms = std::chrono::milliseconds(30000);

    std::stop_source ss;
    ss.request_stop();  // pre-cancel

    auto result = interpreter.run(skill, cap, ss.get_token());

    // 核心契约: token 已透传至 llm_->generate (而非硬编码 {})
    REQUIRE(raw->generate_calls == 1);
    REQUIRE(raw->last_token_stop_requested == true);
    // cleanup...
}
```

## 风险评估

- 生产风险：低（默认 `{}` 参数 + 既有硬编码 `{}` 等价）
- 测试风险：低（RecordingLLMProvider 已有 last_token_stop_requested 字段, Wave 4 fix-orchestrator 添加）
- API 兼容：100% 向后兼容（默认参数）