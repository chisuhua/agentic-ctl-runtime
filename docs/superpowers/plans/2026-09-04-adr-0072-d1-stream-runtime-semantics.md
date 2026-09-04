# ADR-0072 D1 阶段 B: IStreamHandle 运行时流式语义 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 HydraForge (`/workspace/project/HydraForge`) 中落地 ADR-0072 D1 阶段 B — 新增 `IStreamHandle` L1 契约层抽象 + 2 类节点 (tool_call / dsl_call) 运行时流式触发 (V1 切片回放) + `set_stream_sink()` 注入点 + 与 Wave 3-A stop_token 链路集成. ADR-0072 D1 实施度 1/6 → 2/6.

**Architecture:**
- `IStreamHandle` (5 虚函数: 3 consumer pull + 2 producer push/close) — 独立于 `IGenerationStream`, header-only L1 契约层
- 2 个参考实现: `BufferedStreamHandle` (pull 累积) + `CallbackStreamHandle` (push + pull, 内部有界队列 capacity=32, CV 满阻塞)
- `NodeExecutor::set_stream_sink(IStreamHandle*)` 注入点 (raw pointer, no ownership, 与 `set_tool_coordinator` 模式一致)
- 2 类节点 (tool_call / dsl_call) 同步执行完成后切片回放 (post-hoc pseudo-streaming)
- stop_token 消费者侧闭环 (consumer 持 stop_source → `next(token)` 内部检查 stop_requested())

**Tech Stack:** C++20 / Catch2 / nlohmann::json / std::stop_token (Wave 3-A 已 ship)

**OpenSpec change:** `openspec/changes/adr-0072-d1-stream-runtime-semantics/` (4/4 artifacts complete, openspec validate --strict PASS, Oracle 二审 CONDITIONAL → M1/M2/M3 已修复)

**关联计划**:
- [docs/superpowers/plans/2026-07-24-sprint-24-25-demo-driven-plan.md](./2026-07-24-sprint-24-25-demo-driven-plan.md) — Sprint 24-25 master plan
- [docs/superpowers/plans/2026-09-03-u4-agentforge-second-domain-agent.md](./2026-09-03-u4-agentforge-second-domain-agent.md) — U4 (上一 ship 的同类格式参考)
- HydraForge roadmap.md W4 阶段 B — 本计划作为 ADR-0072 D1 阶段 B 的执行载体

**关联 ADR**:
- ADR-0072 DSL Node Extensions (D1 阶段 A 已 ship, 阶段 B = 本计划)
- ADR-0001 ILLMProvider Streaming Interface (`IGenerationStream` 签名参考)
- ADR-0008 Structured Context (LayeredContext 切片源)
- ADR-0071 LLM-native AgenticDSL 架构 (顶层方向)
- ADR-0068 Event Emission Contract (V2 streaming bus 跟进)

---

## 0. 实施前 Checklist

- [ ] 0.1 实测当前 ctest baseline (替换 tasks 7.1 中 "211/212" 数字)
  ```bash
  cd /workspace/project/HydraForge && cmake --preset debug -DAGENTICDSL_BUILD_TESTS=ON && cmake --build build --target test_stream_runtime 2>&1 | tail -5
  ctest --test-dir build --output-on-failure 2>&1 | tail -3
  ```
- [ ] 0.2 确认 git status 干净 (避免污染 change commit)
- [ ] 0.3 创建 worktree (per superpowers:using-git-worktrees)
  ```bash
  cd /workspace/project/HydraForge && git worktree add ../.worktrees/adr-0072-stream-runtime -b adr-0072-d1-stream-runtime
  ```

---

## 1. TDD RED — 写失败测试

### 1.1 创建 test_stream_runtime.cpp 骨架 (5 类测试全失败)

**Goal**: 5 类 TEST_CASE 全失败 (因 IStreamHandle 头文件缺失) — RED 状态建立

- [ ] 1.1.1 新建 `tests/test_stream_runtime.cpp`, 包含 5 类 TEST_CASE:
  - **类别 A (IStreamHandle 契约)**: `i_stream_handle_pull_drains_to_eof`, `i_stream_handle_push_writes_chunk`, `i_stream_handle_close_signals_eof`, `i_stream_handle_close_with_error_sets_error_state`, `i_stream_handle_cancel_via_stop_token`, `i_stream_handle_three_state_distinction` (6 cases)
  - **类别 B (BufferedStreamHandle)**: `buffered_stream_handle_pull_accumulates_chunks`, `buffered_stream_handle_close_marks_eof` (2 cases)
  - **类别 C (CallbackStreamHandle)**: `callback_stream_handle_invokes_callback_on_push`, `callback_stream_handle_pull_drains_to_eof`, `callback_stream_handle_full_queue_blocks_push`, `callback_stream_handle_clear_callback_on_destruct` (4 cases)
  - **类别 D (NodeExecutor stream_sink 注入)**: `node_executor_set_stream_sink_default_null`, `node_executor_tool_call_stream_true_slices_to_sink`, `node_executor_dsl_call_stream_true_slices_to_sink`, `node_executor_stream_false_or_missing_keeps_sync_path`, `node_executor_stream_sink_null_emits_warning` (5 cases)
  - **类别 E (stop_token 传播)**: `stop_token_cancel_observes_in_next_within_100ms`, `stop_token_cancel_mid_push_completes_then_terminates` (2 cases)
- [ ] 1.1.2 总计 ~350 行, 19 TEST_CASE
- [ ] 1.1.3 在 `tests/CMakeLists.txt` 注册 `test_stream_runtime` target
  ```cmake
  add_executable(test_stream_runtime test_stream_runtime.cpp)
  target_link_libraries(test_stream_runtime PRIVATE agenticdsl_core)
  ```

### 1.2 验证 RED 状态

- [ ] 1.2.1 构建 `test_stream_runtime` 应失败 (因 `i_stream_handle.h` 不存在)
  ```bash
  cmake --build build --target test_stream_runtime 2>&1 | grep -E "(error|undefined)" | head -5
  ```
- [ ] 1.2.2 确认失败原因: `fatal error: 'agenticdsl/contract/i_stream_handle.h' file not found`

---

## 2. TDD GREEN — 最小实现 (契约 + 2 个参考实现)

### 2.1 新增 IStreamHandle 契约层

- [ ] 2.1.1 新建 `include/agenticdsl/contract/i_stream_handle.h` (header-only, ~70 行):
  ```cpp
  // ADR-0072 D1 阶段 B: IStreamHandle 契约层
  // 5 虚函数: 3 consumer pull + 2 producer push/close
  // 独立于 IGenerationStream (per ADR-0001)
  #pragma once
  #include <optional>
  #include <string>
  #include <stop_token>
  #include "common/llm/llm_types.h"  // LLMError

  namespace agenticdsl {
  class IStreamHandle {
   public:
    virtual ~IStreamHandle() = default;
    // Consumer 侧 (pull-based)
    virtual std::optional<std::string> next(std::stop_token token) = 0;
    virtual bool is_active() const = 0;
    virtual std::optional<LLMError> error() const = 0;
    // Producer 侧 (per Oracle M1 修复)
    virtual void push(std::string chunk) = 0;
    virtual void close(std::optional<LLMError> err = std::nullopt) = 0;
  };
  }  // namespace agenticdsl
  ```
- [ ] 2.1.2 验证编译成功 (header-only, 0 build 改动)

### 2.2 新增 BufferedStreamHandle + CallbackStreamHandle

- [ ] 2.2.1 新建 `src/common/runtime/stream_handle.h` (~60 行):
  ```cpp
  #pragma once
  #include "agenticdsl/contract/i_stream_handle.h"
  #include <queue>
  #include <mutex>
  #include <condition_variable>
  #include <functional>

  namespace agenticdsl {
  class BufferedStreamHandle : public IStreamHandle {
   public:
    std::optional<std::string> next(std::stop_token) override;
    bool is_active() const override;
    std::optional<LLMError> error() const override;
    void push(std::string chunk) override;
    void close(std::optional<LLMError> err = std::nullopt) override;
   private:
    mutable std::mutex mu_;
    std::queue<std::string> queue_;
    bool active_ = true;
    std::optional<LLMError> error_;
  };

  class CallbackStreamHandle : public IStreamHandle {
   public:
    explicit CallbackStreamHandle(std::function<void(std::string)> on_chunk = nullptr);
    std::optional<std::string> next(std::stop_token) override;
    bool is_active() const override;
    std::optional<LLMError> error() const override;
    void push(std::string chunk) override;
    void close(std::optional<LLMError> err = std::nullopt) override;
   private:
    static constexpr size_t kCapacity = 32;
    mutable std::mutex mu_;
    std::condition_variable cv_full_;  // 唤醒 push 等待者
    std::queue<std::string> queue_;
    bool active_ = true;
    std::optional<LLMError> error_;
    std::function<void(std::string)> on_chunk_;
  };
  }  // namespace agenticdsl
  ```
- [ ] 2.2.2 新建 `src/common/runtime/stream_handle.cpp` (~100 行):
  - `BufferedStreamHandle::next` 加锁检查 active_/queue_/stop_token; 队列空且 active_ 返回 nullopt; 否则弹出
  - `BufferedStreamHandle::push` 加锁入队 (active_ 检查, 否则 no-op)
  - `BufferedStreamHandle::close` 加锁设置 active_=false + error_; idempotent
  - `~BufferedStreamHandle` RAII: 加锁 mark inactive → 清队列 → 析构 (per Decision 7)
  - `CallbackStreamHandle::push` 加锁入队 (capacity=32 满则 cv_full_.wait 阻塞); 若 on_chunk_ 注册则同步触发
  - `CallbackStreamHandle::close` 同 BufferedStreamHandle + notify_all() 唤醒 push 等待者
  - `~CallbackStreamHandle` RAII: 加锁 mark inactive → 清队列 → 清 on_chunk_ → notify_all() 唤醒阻塞 push
- [ ] 2.2.3 新建 `src/common/runtime/CMakeLists.txt`:
  ```cmake
  add_library(agenticdsl_runtime STATIC stream_handle.cpp)
  target_include_directories(agenticdsl_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
  ```
- [ ] 2.2.4 在根 `CMakeLists.txt` 添加 `add_subdirectory(src/common/runtime)` (若尚未包含)
- [ ] 2.2.5 在 `agenticdsl_core` target 中链接 `agenticdsl_runtime`

### 2.3 验证 GREEN 状态 (类别 A + B + C 应 PASS)

- [ ] 2.3.1 构建 `test_stream_runtime`:
  ```bash
  cmake --build build --target test_stream_runtime 2>&1 | tail -5
  ```
- [ ] 2.3.2 运行类别 A/B/C 测试:
  ```bash
  ctest --test-dir build -R "test_stream_runtime" --output-on-failure 2>&1 | tail -10
  ```
- [ ] 2.3.3 期望: 12 cases PASS (A: 6 + B: 2 + C: 4), 7 cases FAIL (D: 5 + E: 2 — 因 NodeExecutor 尚未注入 sink)

---

## 3. TDD IMPL — NodeExecutor 改造 (2 节点 stream 分支 + sink 注入点)

### 3.1 node_executor.h 添加 sink 注入点

- [ ] 3.1.1 修改 `src/modules/executor/node_executor.h`:
  - 在 includes 区添加 `#include "agenticdsl/contract/i_stream_handle.h"`
  - 在 class 顶部 (private 区域) 添加:
    ```cpp
    // ADR-0072 D1 阶段 B: stream 注入点 (per Decision 3)
    IStreamHandle* stream_sink_ = nullptr;
    static constexpr size_t kStreamChunkSize = 64;
    ```
  - 在 public 区域添加:
    ```cpp
    void set_stream_sink(IStreamHandle* sink) { stream_sink_ = sink; }
    ```
- [ ] 3.1.2 验证编译通过

### 3.2 node_executor.cpp 改造 2 类节点 stream 分支

- [ ] 3.2.1 修改 `execute_tool_call` (`src/modules/executor/node_executor.cpp:176`):
  - 在函数顶部添加 stream 分支决策:
    ```cpp
    // ADR-0072 D1 阶段 B: 同步路径不变 + stream 分支 (per Decision 10)
    bool is_stream_requested = node->metadata.is_object() &&
        node->metadata.contains("stream") &&
        node->metadata["stream"].is_boolean() &&
        node->metadata["stream"].get<bool>();
    if (is_stream_requested && stream_sink_ != nullptr) {
      // 执行同步路径
      Context result_ctx = /* 既有逻辑 */;
      // 切片 tool_result.data.dump() 推送至 sink
      std::string text = /* 从 result_ctx 提取 */;
      push_stream_chunks(text);  // helper
      stream_sink_->close(std::nullopt);
      return result_ctx;
    }
    if (is_stream_requested && stream_sink_ == nullptr) {
      std::cerr << "[WARN] stream:true ignored: no sink registered" << std::endl;
    }
    // 既有同步路径 (Decision 10 严格不变)
    ```
  - 添加 helper `void NodeExecutor::push_stream_chunks(const std::string& text)`:
    ```cpp
    // 切片 text 为 N 个 chunk, push 至 stream_sink_
    size_t total = text.size();
    for (size_t offset = 0; offset < total; offset += kStreamChunkSize) {
      stream_sink_->push(text.substr(offset, std::min(kStreamChunkSize, total - offset)));
    }
    ```
- [ ] 3.2.2 修改 `execute_dsl_node` 同样模式 (读 metadata["stream"] + 同步路径 + 切片 sink_)
- [ ] 3.2.3 验证既有 100+ 测试零回归 (Decision 10):
  ```bash
  ctest --test-dir build --output-on-failure 2>&1 | tail -3
  ```

### 3.3 验证 GREEN 状态 (类别 D + E 应 PASS)

- [ ] 3.3.1 重新构建并运行 5 类测试:
  ```bash
  cmake --build build --target test_stream_runtime 2>&1 | tail -5
  ctest --test-dir build -R "test_stream_runtime" --output-on-failure 2>&1 | tail -10
  ```
- [ ] 3.3.2 期望: 19/19 PASS (A:6 + B:2 + C:4 + D:5 + E:2)

---

## 4. 文档同步

### 4.1 dsl.md REQ-W4-001 阶段 B 章节

- [ ] 4.1.1 修改 `docs/specs/dsl.md` REQ-W4-001 章节 (append 阶段 B 子节):
  ```markdown
  ### REQ-W4-001-B: 运行时流式语义 (阶段 B, 2026-09-04 ship)

  **V1 语义**: 节点同步执行完成后切片回放 (post-hoc pseudo-streaming). 真流式 (incremental) 依赖 IToolRegistry streaming API, V2 阶段交付.

  **触发条件**: `metadata["stream"] == true` (严格 JSON 布尔, 缺省/false/字符串/数字/null 不触发).

  **执行路径**:
  1. NodeExecutor 调 `set_stream_sink(sink_ptr)` 注入 IStreamHandle* (raw pointer)
  2. 同步执行 (tool_call 调 call_tool, dsl_call 调 call_llm_tool) — 与既有路径完全一致
  3. 完成后将 ToolResult/Context 切片为 N 个 chunk (kStreamChunkSize=64), 通过 `sink_->push(chunk)` 写入
  4. 推完后 `sink_->close(std::nullopt)` 标记正常 EOF

  **消费者侧**: consumer 通过 `sink_->next(token)` 拉取; cancel 通过 consumer 持 stop_source → `request_stop()` → `next(token)` 内部检查 stop_requested() 终止.

  **scope**: 2 类节点 (tool_call / dsl_call); shell_exec NodeType 不存在 (W5 parser 提案).
  ```

### 4.2 adr-0072-dsl-node-extensions.md D1 实施度更新

- [ ] 4.2.1 修改 `docs/adr/adr-0072-dsl-node-extensions.md`:
  - 翻牌时点状态表 D1 行: ❌ 未实施 → ✅ **阶段 A + 阶段 B ship** (parser 字段 + 2 节点 runtime)
  - 新增 V1 切片回放语义注记

### 4.3 active-status.md Sprint 25 carry-over W4 阶段 B closed

- [ ] 4.3.1 修改 `docs/active-status.md`:
  - W4 阶段 B (IStreamHandle 语义, 4h, P0) → ✅ closed 2026-09-04 (commit `...`)
  - ADR-0072 D1 实施度 1/6 → 2/6

---

## 5. 验证 + Archive + Commit

### 5.1 全量回归

- [ ] 5.1.1 全量 ctest 回归:
  ```bash
  cmake --preset debug -DAGENTICDSL_BUILD_TESTS=ON && cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure 2>&1 | tail -10
  ```
- [ ] 5.1.2 期望: 既有 baseline + 5 类 19 cases PASS (新增), 0 新增 regression
- [ ] 5.1.3 `tools/adr_lint.py`:
  ```bash
  python3 tools/adr_lint.py 2>&1 | tail -3
  ```
- [ ] 5.1.4 `openspec validate`:
  ```bash
  openspec validate adr-0072-d1-stream-runtime-semantics --strict --json 2>&1 | tail -5
  ```

### 5.2 Oracle ac-verifier 验收

- [ ] 5.2.1 调用 ac-verifier skill 验证 acceptance criteria
- [ ] 5.2.2 PASS: 进入 5.3 archive
- [ ] 5.2.3 FAIL: 根据 Oracle 反馈修订实施, 重跑 5.1

### 5.3 Archive + Commit + Push

- [ ] 5.3.1 archive change:
  ```bash
  openspec archive adr-0072-d1-stream-runtime-semantics --yes 2>&1 | tail -5
  ```
- [ ] 5.3.2 更新 `iteration.json` (+1 archived entry):
  ```bash
  # 自动由 openspec archive 完成
  ```
- [ ] 5.3.3 git commit (1 atomic commit per change):
  ```bash
  git add -A && git status --short
  git commit -m "feat(executor): ADR-0072 D1 阶段 B IStreamHandle runtime semantics (Sprint 26 W4 阶段 B)

  TDD 5 步 (RED→GREEN→IMPL→VERIFY→COMMIT):
  - include/agenticdsl/contract/i_stream_handle.h: 5 虚函数契约层 (3 pull + 2 push/close)
  - src/common/runtime/stream_handle.{h,cpp}: BufferedStreamHandle + CallbackStreamHandle (RAII)
  - src/modules/executor/node_executor.{h,cpp}: set_stream_sink() 注入点 + 2 节点 stream 分支 (V1 切片回放)
  - tests/test_stream_runtime.cpp: 5 类 TEST_CASE / 19 cases PASS (A 契约 / B Buffered / C Callback / D sink 注入 / E stop_token)
  - docs/specs/dsl.md REQ-W4-001-B: 运行时流式语义章节 (V1 切片回放 + scope 收缩注记)
  - docs/adr/adr-0072-dsl-node-extensions.md: D1 实施度 1/6 → 2/6
  - docs/active-status.md: W4 阶段 B closed

  估计: 4h (scope 收缩 + V1 切片回放降级真流式估时)

  Test: 19/19 PASS, 全量 ctest 零回归
  ADR-0072 D1: 1/6 → 2/6 (阶段 A 字段层 + 阶段 B 运行时)"
  ```
- [ ] 5.3.4 push to origin/main:
  ```bash
  git push origin adr-0072-d1-stream-runtime
  # 或 main 分支 (若 worktree 在 main)
  ```
- [ ] 5.3.5 清理 worktree:
  ```bash
  git worktree remove ../.worktrees/adr-0072-stream-runtime
  git branch -d adr-0072-d1-stream-runtime
  ```

---

## 6. Risk Mitigation (实施期关注)

| 风险 | 缓解 |
|------|------|
| IStreamHandle 头文件被其他模块污染 | header-only + 严格 namespace agenticdsl |
| stream_sink_ 跨线程访问 data race | sink 由 consumer 管理生命周期, NodeExecutor 仅同步 push |
| Decision 10 既有 100+ 测试零回归 | 既有同步路径严格分支隔离, 仅当 metadata["stream"] == true && sink != nullptr 时进入 stream 分支 |
| 切片丢失边界 chunk | kStreamChunkSize=64, text.substr(offset, min(64, total-offset)) 保证末尾 |
| producer 异常 (call_tool 抛错) | 流切片在同步执行成功后才开始, 异常走原错误传播路径, 不进入 stream 分支 |

---

## 7. Total Estimated Time

| Step | 估时 |
|------|------|
| 0. 实施前 Checklist | 10 min |
| 1. TDD RED 写失败测试 | 30 min |
| 2. TDD GREEN 契约 + 双实现 | 60 min |
| 3. TDD IMPL NodeExecutor 改造 | 45 min |
| 4. 文档同步 | 20 min |
| 5. 验证 + Archive + Commit | 25 min |
| **Total** | **~3.5h** (估时 4h 留 buffer) |
