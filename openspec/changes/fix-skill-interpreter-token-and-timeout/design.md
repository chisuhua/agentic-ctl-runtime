# fix-skill-interpreter-token-and-timeout — Design

## 接口改动

### `include/agenticdsl/skill/skill_interpreter.h`

```cpp
class SkillInterpreter {
 public:
  SkillResult run(const std::string& skill_path,
                  const SkillCapability& cap,
                  std::stop_token token = {});  // NEW
};
```

### `src/modules/skill_interpreter/skill_interpreter.cpp`

```cpp
class SkillInterpreter::Impl {
  SkillResult run(const std::string& skill_path,
                  const SkillCapability& cap,
                  std::stop_token token) {
    SkillResult result = ipc_loop_and_wait(
        pid, pipe_out[0], pipe_in[1], pipe_err[0], cap, token);
    // ...
  }

  SkillResult ipc_loop_and_wait(pid_t pid, int pipe_out_r, int pipe_in_w,
                                 int pipe_err_r, const SkillCapability& cap,
                                 std::stop_token token = {}) {
    while (true) {
      auto now = std::chrono::steady_clock::now();
      auto remaining = deadline - now;

      // 外部 cancel → 立即 SIGKILL (不等到 cap.timeout_ms)
      if (token.stop_requested()) {
        kill_retry(pid, SIGKILL);
        int status;
        waitpid_reap(pid, &status);
        SkillResult r;
        r.success = false;
        r.error_code = ErrorCode::Abort;
        // ...
        return r;
      }

      if (remaining <= std::chrono::nanoseconds(0)) { /* Timeout */ }
      // ... poll loop
    }
  }
};
```

## 路径分析

```
外部 cancel token → SkillInterpreter::run → Impl::run → ipc_loop_and_wait
→ while(true) { if (token.stop_requested()) → kill_retry(SIGKILL) + waitpid_reap + ErrorCode::Abort }
```

## 测试设计

### Test 7.8b (新增)

```cpp
TEST_CASE("7.8b pre-cancelled stop_token triggers immediate SIGKILL",
          "[skill_interpreter][token][realllm-gap-fix]") {
    SkillCapability cap;
    cap.allowed_tools = {"fs.read"};
    cap.max_steps = 100;  // 高, 避免 max_steps SIGKILL 干扰
    cap.timeout_ms = std::chrono::milliseconds(30000);  // 长, 证明 token 立即生效

    std::stop_source ss;
    ss.request_stop();

    auto start = std::chrono::steady_clock::now();
    auto result = interpreter.run(skill, cap, ss.get_token());
    auto elapsed = ...;

    CHECK_FALSE(result.success);
    CHECK(result.error_code == ErrorCode::Abort);
    CHECK(elapsed < 5000);
}
```

## 风险评估

- 生产风险：低（默认 `{}` 参数 + 既有无限等待被替换为正确 SIGKILL 行为）
- 测试风险：低（Test 7.8b 使用 pre-cancel, 独立于 max_steps / timeout）
- API 兼容：100% 向后兼容