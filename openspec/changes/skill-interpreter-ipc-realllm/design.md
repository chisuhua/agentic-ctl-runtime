# Design — skill-interpreter-ipc-realllm

## 务实范围 (Phase E 1h tractable)

Phase E 原计划 5 cases (E.1-E.4 + GAP E.5)，全量 4h+ tractability。Wave 3 时序约束下
**scope 收缩**：本 change 仅 E.1（host function direct test），E.2-E.4 延后到独立
change（`skill-interpreter-ipc-e2e-realllm`），E.5 记录 GAP 留给 Wave 4。

## 测试架构

### 测试目标

在 `tests/test_skill_interpreter.cpp`（既有文件）追加 1 case，验证
`SkillInterpreterImpl::dispatch_llm_generate` host function 在真实 deepseek
LLM 下的端到端契约（host function direct test, 无 subprocess）：

| Case | 验证路径 | 期望 |
|---|---|---|
| E.1 | `dispatch_llm_generate` 真实 LLM | IPCResponse{ok=true, data.content=<真实文本>}, 不撞墙 |

### 关键 API 变化: 公开 host function wrapper

`SkillInterpreterImpl::dispatch_llm_generate` 当前是 private member（friend 测试
或全 friend class 侵入）。Phase E 设计: 公开 test-only wrapper:

```cpp
// include/agenticdsl/skill/skill_interpreter.h (新增 public API)
class SkillInterpreter {
 public:
  // ... 既有 API ...

  // [[deprecated("test only; host function for unit test direct invocation,
  //              NOT equivalent to full IPC subprocess path")]]
  // Phase E 务实范围: 公开 host function 让单元测试可直调, 无需 posix_spawn 子进程.
  // production 调用走 execute() → spawn child → child IPC → parent dispatch_llm_generate.
  // E.2-E.4 follow-up (skill-interpreter-ipc-e2e-realllm) 验证 full subprocess 路径.
  IPCResponse call_llm_generate_for_test(
      const IPCRequest& req,
      const SkillCapability& cap) {
    return impl_->dispatch_llm_generate(req, cap);
  }

 private:
  std::unique_ptr<Impl> impl_;  // PIMPL 模式封装 dispatch_llm_generate
};
```

注: `dispatch_llm_generate` 当前已是 SkillInterpreterImpl private member, PIMPL 模式
封装后 `call_llm_generate_for_test()` 在 SkillInterpreter public API 上转发到
`impl_->dispatch_llm_generate(req, cap)`。零生产路径侵入, 仅为 unit test 暴露。

### SkillCapability 准备

```cpp
SkillCapability cap;
cap.allow_llm = true;  // 关键: dispatch_llm_generate 检查此字段
cap.budget_limit_usd = 1.0;  // $1 上限 (足够 1 call)
```

### IPCRequest 构造

```cpp
IPCRequest req;
req.call = "llm_generate";
req.params["prompt"] = "Say hello";
req.params["model"] = "deepseek-v4-flash";  // 显式 (防御)
// req.params.metadata 可选 (host function 不读)
```

### 单元测试设计 (1 case)

#### E.1 — dispatch_llm_generate 真实 LLM host function direct test

```cpp
TEST_CASE("SkillInterpreter IPC dispatch_llm_generate real LLM host",
          "[skill_interpreter][realllm][phase-e][e1]") {
  agenticdsl::test::require_real_llm_env();
  if (agenticdsl::test::real_llm_env_skipped()) {
    SUCCEED("skipped: HYDRAFORGE_SKIP_REAL_LLM=1 (no API key or CI skip)");
    return;
  }

  // 构造 SkillInterpreter (parent 端) + 真实 LLM provider + SkillCapability
  auto bus = std::make_shared<InMemoryBus>();
  auto llm_provider = agenticdsl::test::real_llm_provider();
  auto skill = SkillInterpreter::create_for_test(bus, std::move(llm_provider));

  SkillCapability cap;
  cap.allow_llm = true;
  cap.budget_limit_usd = 1.0;

  IPCRequest req;
  req.call = "llm_generate";
  req.params["prompt"] = "Say OK in one word";
  req.params["model"] = "deepseek-v4-flash";

  // 直接调 host function (无 posix_spawn 子进程, 无 pipe read/write)
  auto response = skill.call_llm_generate_for_test(req, cap);

  // 核心契约: 真实 LLM 成功返回 (model 遮蔽已 ship 修复)
  REQUIRE(response.ok);
  REQUIRE(response.data.contains("content"));
  REQUIRE_FALSE(response.data["content"].get<std::string>().empty());

  // GAP 验证 (E.5): 真实 LLM hang 时无 stop_token 超时保护
  // (本测试不验证 hang 路径, 仅记录 GAP 在 tasks.md §E.5)
}
```

### E.5 GAP 文档化 (非代码改动, tasks.md 记录)

```markdown
## Phase E — E.5 GAP 记录

- **GAP 位置**: `src/modules/skill_interpreter/skill_interpreter.cpp:659`
  `SkillInterpreterImpl::dispatch_llm_generate`
- **GAP 现象**: 函数签名无 `std::stop_token` 参数, 真实 LLM hang 时
  子进程永久 block, 无法外部取消
- **影响**: Wave 3 Phase E 单元测试 OK (本 change), 但生产场景
  child process IPC 调用 dispatch_llm_generate 时若 deepseek hang,
  child 永久 block 直至父进程 kill
- **归属**: Wave 4 `fix-skill-interpreter-token-and-timeout` 独立 change
- **估时**: 0.5-1 day (token 透传 + std::async timeout + 子进程优雅退出)
```

### CI / 验证流程

1. **CI 默认 skip** (`HYDRAFORGE_SKIP_REAL_LLM=1`): 1 case SUCCEED early return
2. **本地有 key**: 真实 LLM 跑 — E.1 验证 host function direct path
3. **无 key 无 skip**: helper FAIL 硬门槛

### 失败诊断

```cpp
if (!response.ok) {
  std::cerr << "[diag:E.1] response.ok=false, error=" << response.error_message << "\n";
}
if (response.data.contains("llm_error")) {
  std::cerr << "[diag:E.1] LLM error: " << response.data["llm_error"].dump() << "\n";
}
```

## 实施顺序

1. **Phase 1 (test wrapper + tests)**:
   - 公开 `call_llm_generate_for_test()` 在 skill_interpreter.h (test-only wrapper)
   - 实现 impl_->dispatch_llm_generate(req, cap) 转发
   - `tests/test_skill_interpreter.cpp` 追加 1 case + skip
2. **Phase 2 (验证)**:
   - skip 模式 ctest: 1 case SUCCEED + N mock cases PASS, baseline 231 → 232
   - 真实 deepseek: 本地有 key 跑
3. **Phase 3 (commit + archive)**:
   - 1 commit (wrapper + test) + archive

每步骤独立 commit (atomic commit 原则).

## follow-up (本 change archive 时记入)

- **E.2-E.4 full execute end-to-end**: 独立 change `skill-interpreter-ipc-e2e-realllm`
  (SKILL.md fixture + posix_spawn subprocess + 50KB pipe buffer), 估时 4h+
- **E.5 GAP fix**: Wave 4 `fix-skill-interpreter-token-and-timeout`
  (dispatch_llm_generate 加 stop_token + 子进程优雅退出), 估时 0.5-1 day