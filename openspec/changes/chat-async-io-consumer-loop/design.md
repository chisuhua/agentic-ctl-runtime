## Context

`examples/pdk_chat_demo` 当前实现存在 stdin 双读 race 与 queue dead-producer 双重缺陷：

**双读 race**：`main.cpp:547` 的 `while (std::getline(std::cin, input))` 与 `ChatSession::Impl::input_thread_`（`chat_session.cpp:627-651` `input_thread_main()`）同时从 stdin 读，OS 调度不可预测地让其中一方拿到首字符。

**Dead-producer**：input thread 把字符塞进 `steering_queue_` / `follow_up_queue_` 后，**没有任何代码消费这两个队列**。`grep -rn "follow_up_queue_\|steering_queue_" examples/pdk_chat_demo/ src/` 显示只有 4 处引用：字段声明 + input_thread 生产 + queue_size/try_clear_queue/try_push_*_for_test 测试 helper。`ChatSession::chat()` 与 `main.cpp` 都不消费队列。

**c30b2b3 的 partial fix**：把 `enable_input_thread = true` 改成 `isatty(STDIN_FILENO) != 0` — 仅修复 pipe 模式（自动化探测场景），TTY 模式下 input thread 仍启动，仍 race。现场复现 2026-09-07 15:54:38（详见 `proposal.md`）证实两个症状仍存在。

**Spec 留白**：`chat-async-queue-infra` spec 标题 "Input thread produces, **ChatSession consumes**" 但所有 Scenario 只定义 producer 行为，consumer side 从未 ship。Phase B (`chat-async-cancellation-chain`) 与 Phase C (`chat-async-io-model-switching`) 假设 consumer 已存在但实际不存在 — 这是一个 ship 时遗留的契约缺口。

## Goals / Non-Goals

**Goals:**
- 闭合 producer-consumer 闭环：main loop 是队列的**唯一 consumer**，input thread 是 stdin 的**唯一 reader**
- TTY 与 pipe 双模式下都使用同一套 single-reader 代码路径，消除 isatty 分支
- steering queue 中断机制：用户在 turn 执行中输入 `/cancel` 应在 ≤500ms 内停止当前 turn
- follow_up queue 在 turn 完成后自动 drain（不堆积）
- 全部 4 个验证场景（单 reader 模式无 race / steering interrupt / follow-up drain / pipe E2E）通过
- 严格向后兼容：现有 chat_session 测试 100% 通过，外部 API 仅新增不修改

**Non-Goals:**
- 不重写 input thread 的 producer 逻辑（d4fcca1 已 ship 且行为正确）
- 不修改 `chat-async-cancellation-chain` 的 stop_token 链路（d1ecca2 已 ship），steering interrupt 仅复用其 CancellationRegistry
- 不修改 `/model` 命令的 Wave 3-A Phase C 实现（526c88b 已 ship），仅通过 steering queue 接入
- 不引入新的并发原语（仅用 std::mutex + std::condition_variable 组合）
- 不修改 chat_session.h 的 `enable_input_thread` 字段类型（保留 bool，默认值改为 true）
- 不修复其他已 archive change 中遗留的 YAML DSL 校验问题（独立 follow-up）

## Decisions

### Decision 1: Single-Reader Pattern (核心架构)

**选择**：main loop **不直接调用 `std::getline(std::cin, ...)`**，改为通过 `session.pop_next_input(timeout_ms)` 阻塞等待 input thread 入队的消息。input thread 成为 stdin 的唯一 reader。

**理由**：
- 消除 race 的根本方法不是同步（stdin 不可被 mutex 保护），而是 single-reader 模式
- 与 chat-async-queue-infra spec 标题 "Input thread produces, ChatSession consumes" 对齐
- 撤销 c30b2b3 的 `isatty` 守卫：single-reader 模式下 race 已消除，无需区分 TTY/pipe

**替代方案**：
- A. 加 mutex 保护 `std::getline` — 不可行，`std::cin` 内部有 buffer lock，跨 thread 调用是 UB
- B. 用 `select()` / `poll()` 多路复用 — 需要引入 POSIX API 与 fd 管理，与 chat-async-io 设计哲学不符
- C. **采纳**：single-reader 是最简单且 spec-aligned 的方案

**API 设计**：
```cpp
// chat_session.h 新增
struct InputMessage {
  enum class Kind { Steering, FollowUp };
  Kind kind;
  std::string text;
};

// 非阻塞弹出，返回 std::optional<InputMessage>
std::optional<InputMessage> try_pop_input();

// 阻塞等待直到有消息或超时或 shutdown
std::optional<InputMessage> pop_next_input(std::chrono::milliseconds timeout);
```

### Decision 2: Condition Variable 实现 pop_next_input (含 C2/C3 修复)

**选择**：`pop_next_input()` 用 `std::condition_variable` + `std::mutex` 实现，**+ 引入 `std::atomic<size_t> pending_input_count_`** 作为 producer/consumer 之间的同步计数器，以消除 Oracle C2（data race）和 C3（丢失唤醒窗口）。

**理由**：
- 避免 busy-loop 浪费 CPU
- `pending_input_count_` (atomic) 替代直接在 predicate 里读 `queue.empty()` —— 消除与 input thread `push` 的并发读竞争（TSan 验证）
- push → fetch_add → notify_one 顺序保证：即使 push 发生在 main loop `wait_for` 谓词检查之后，pending_count 仍单调递增，下一次 wait_for 唤醒时立即看到消息（避免 C3 丢失唤醒）
- shutdown 信号沿用现有 `stop_input_thread_` atomic

**Oracle findings 修复映射**：
- **C2 (data race on `queue.empty()`) → 修复**: predicate 改读 atomic `pending_input_count_.load() > 0`，不再无锁读 queue
- **C3 (lost wakeup) → 修复**: push 在持队列 mutex 下递增 pending_count，再 notify_one；即使 main loop 错过本次 predicate 检查，下一次 wait_for 仍能看到 pending > 0

**具体实现**：
```cpp
// chat_session.cpp Impl 新增字段
std::condition_variable input_cv_;
std::mutex input_cv_mutex_;
std::atomic<size_t> pending_input_count_{0};  // C2/C3 修复核心
std::atomic<bool> shutdown_{false};            // 现有 stop_input_thread_ 复用

std::optional<InputMessage> Impl::try_pop_input() {
  InputMessage msg;
  // 先看 steering（priority: 优先级高）
  bool popped = false;
  {
    std::lock_guard<std::mutex> lock(steering_mutex_);
    if (!steering_queue_.empty()) {
      msg.kind = InputMessage::Kind::Steering;
      msg.text = std::move(steering_queue_.front());
      steering_queue_.pop();
      popped = true;
    }
  }
  if (!popped) {
    std::lock_guard<std::mutex> lock(follow_up_mutex_);
    if (!follow_up_queue_.empty()) {
      msg.kind = InputMessage::Kind::FollowUp;
      msg.text = std::move(follow_up_queue_.front());
      follow_up_queue_.pop();
      popped = true;
    }
  }
  if (popped) {
    pending_input_count_.fetch_sub(1, std::memory_order_acq_rel);
    return msg;
  }
  return std::nullopt;
}

std::optional<InputMessage> Impl::pop_next_input(
    std::chrono::milliseconds timeout) {
  // 检查是否有 pending（避免持锁 cv 时发生不必要的等待）
  if (pending_input_count_.load(std::memory_order_acquire) > 0) {
    auto msg = try_pop_input();
    if (msg) return msg;
  }
  if (shutdown_.load(std::memory_order_acquire)) return std::nullopt;

  std::unique_lock<std::mutex> lock(input_cv_mutex_);
  // predicate: shutdown OR pending > 0
  bool woken = input_cv_.wait_for(lock, timeout, [this]() {
    return shutdown_.load(std::memory_order_acquire) ||
           pending_input_count_.load(std::memory_order_acquire) > 0;
  });
  if (!woken) return std::nullopt;  // 超时
  if (shutdown_.load(std::memory_order_acquire)) return std::nullopt;
  lock.unlock();
  return try_pop_input();
}
```

**input thread enqueue 时**（关键修复 — push 与 count 同步）：
```cpp
// chat_session.cpp input_thread_main() 修改
if (line.empty()) continue;
if (line.front() == '/') {
  std::lock_guard<std::mutex> lock(steering_mutex_);
  if (steering_queue_.size() < capacity_) {
    steering_queue_.push(line);
    pending_input_count_.fetch_add(1, std::memory_order_release);  // C2/C3 修复
    input_cv_.notify_one();
  } else { /* stderr warn */ }
} else {
  std::lock_guard<std::mutex> lock(follow_up_mutex_);
  if (follow_up_queue_.size() < capacity_) {
    follow_up_queue_.push(line);
    pending_input_count_.fetch_add(1, std::memory_order_release);  // C2/C3 修复
    input_cv_.notify_one();
  } else { /* stderr warn */ }
}
```

**测试验证要求**：
- `cmake --preset tsan -DAGENTICDSL_BUILD_TESTS=ON && ctest -R chat_session_consumer` 必须零 race warning（暴露 C2 修复）
- 8.4 新增 TSan-specific 测试：200ms 内并发 1000 次 push/pop，验证 atomic 同步正确
- 9.2 ship gate 必跑 `cmake --preset tsan`（见 tasks §9 更新）

**Oracle NC2 修复 — 计数器不变量 (CRITICAL)**：

所有修改 queue 元素数的代码路径 MUST 同步维护 `pending_input_count_`，以保证不变量 `count == sum(steering_queue_.size() + follow_up_queue_.size())`：

| 代码路径 | push 同步 | clear 同步 | 备注 |
|---|---|---|---|
| `input_thread_main()` (line 627-651) push | ✅ `fetch_add(1, release)` (tasks 3.1-3.2) | N/A | 已修复 |
| `try_pop_input()` pop | ✅ `fetch_sub(1, acq_rel)` 成功后 | N/A | 设计中已有 |
| `try_push_steering_for_test()` | ⚠️ **必须** 加 `fetch_add(1, release)` | N/A | **NC2 修复点 1** |
| `try_push_follow_up_for_test()` | ⚠️ **必须** 加 `fetch_add(1, release)` | N/A | **NC2 修复点 2** |
| `try_clear_queue()` | N/A | ⚠️ **必须** 重置 count 为 0 或 `fetch_sub(N)` | **NC2 修复点 3** |
| overflow 拒绝分支（capacity 满） | ❌ 不加（不入队） | N/A | 不变量保持 |

**不变量违反后果**：
- helper push 不加 count → 8.2/8.3/8.4 测试调用 pop 时 fast-path `count == 0` 但 queue 非空 → pop_next_input 阻塞到 timeout → 测试失败/闪烁
- helper clear 不重置 count → count 残留 > 0 → fast-path 误以为有消息 → 实际 queue 空 → try_pop 返回 nullopt → busy-loop（predicate 恒真）+ size_t 下溢风险

**修复位置**：tasks §2.5-§2.7（helper 同步子任务）+ tasks §8 测试用例必须使用保持不变量后的 helper。

**替代方案**（已弃用）：
- (a) 单一 `input_cv_mutex_` 同时保护队列与 cv — 增加队列 mutex 持锁时间，影响 input thread 吞吐
- (b) **采纳**: atomic 计数器更轻量，且无 data race

**R3 更新**（risk table）：原来的 spurious wakeup 风险仍存在但已通过 `pending_input_count_` 计数保证可见性，spurious wakeup 只导致多一次 try_pop 调用（O(1) 无副作用），无需特殊处理。

### Decision 3: Steering Interrupt 通过 SHARED CancellationRegistry 协同（Oracle NH1/R3 收口）

**选择**：`ChatSession::chat()` 在执行 `loop.run` 期间定期（每 100ms）调用 `try_peek_input()`（**不消费消息**，仅看队头）检查是否有 steering 消息，发现 `/cancel` 时**才**调 `try_pop_input()` 消费该消息，然后从 **`impl_->cancellation_registry_`**（同源注册实例）解析 source 并 `request_stop()` 触发 stop_token 链路。

**理由**：
- 复用 `chat-async-cancellation-chain` (d1ecca2) 已 ship 的 stop_token 链路，不引入新机制
- 100ms 粒度足够响应（用户感知 <500ms）
- `/cancel` 是 steering 优先级的典型用例（其他如 `/model` 在 turn 完成后才处理）
- **关键修复 (Oracle C1)**：`cancellation_registry_` 必须是**全局共享实例**（见 Decision 7），interrupt_thread 与 loop_agent 必须查同一 registry
- **关键修复 (Oracle NH1 + R3)**：interrupt_thread **必须用 peek + 命中才 pop**，而非无条件 pop-all — 否则 turn 期间用户 follow-up 输入会被静默丢弃
- **关键修复 (Oracle R3 H2)**：interrupt_thread **从 `impl_->cancellation_registry_` resolve**（而非全局），与 4.0.5 的注册路径同源 — 消除 test/embedding 路径下全局为 null 时的崩溃隐患（main.cpp 中二者巧合同对象，所以 main 行为不变）

**实现位置**：`chat_session.cpp::chat()` line ~325 `loop/run` 调用前/后插入 polling 循环：

```cpp
// 新增：chat() 内的 steering interrupt poll（Oracle NH1/R3 收口版）
#include "commands/cancellation_globals.h"  // 注：使用新建的 cancellation_globals.h（非 command_globals.h）
std::thread interrupt_thread([this, cancellation_id]() {
  while (!stop_poll_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // 步骤 1：peek（不消费）
    auto peeked = try_peek_input();
    if (peeked && peeked->kind == InputMessage::Kind::Steering &&
        peeked->text == "/cancel") {
      // 步骤 2：仅命中 /cancel 才 pop 消费（其他消息保留给 main loop）
      auto popped = try_pop_input();
      if (popped && popped->kind == InputMessage::Kind::Steering &&
          popped->text == "/cancel") {
        // 步骤 3：从 impl_ 的注册实例（同源）解析 source → 触发 stop
        auto source = impl_->cancellation_registry_->resolve_source(cancellation_id);
        if (source) source->request_stop();
      }
      break;
    }
  }
});
// ... 调用 loop/run ...
// ... 调用结束后:
stop_poll_.store(true);
if (interrupt_thread.joinable()) interrupt_thread.join();
```

**风险**：
- interrupt_thread 与 input_thread 共享 queue：peek 操作仅读队头（持 mutex），pop 仅在 /cancel 命中时执行 → 不与 main loop 竞争消费（main loop 在 turn 期间阻塞在 chat() 内，未并发 pop）
- interrupt_thread 与 chat() 退出 join 时序由 GuardChatScope RAII 保证（R2）
- **新风险 (R3)**: 如果 turn 极快（mock 模式毫秒级完成），interrupt_thread 可能轮询不到 `/cancel`（100ms 粒度 vs LLM 完成时间）→ `/cancel` 留在 queue → main loop 下次 pop → "unknown command" → 这是 H1 的根本原因，需注册 `/cancel` 命令作为兜底（tasks §7.x 新增）

### Decision 4: ~~Follow-Up Drain 在 turn 完成后自动执行~~ (SUPERSEDED by NC1 fix)

**⛔ SUPERSEDED** — **不要实现本决策**。本决策已被 Oracle R3 修复 NC1 推翻。

**原选择（已废弃）**：`chat()` 在调用 `loop/run` **之前**先 drain `follow_up_queue_` 的所有消息。

**废弃原因（NC1 double-consumption）**：
- main loop 在 `while (auto msg = session.pop_next_input(500ms))` 中已 pop FollowUp 消息并调 `session.chat(input)`（tasks §7.5.2）。
- 若 chat() 内部再 drain 同一队列，每条消息被处理两次（一次被 drain 进 history，一次被 main loop 重新 chat）→ 用户可见重复回复。
- 与 spec Req 5 "Follow-up messages are processed in order by the main loop"（main loop 是唯一消费者）直接冲突。

**新设计（替代本决策）**：
- **main loop 是 follow_up_queue_ 唯一消费者**（tasks §7.5.2）。
- `chat()` SHALL NOT drain `follow_up_queue_`（spec Req 5 显式要求）。
- 一次性处理已迁移至 tasks §6 的删除占位 + tasks §7.5.2 的唯一路径。

**保留本段仅作历史**：供 implementer 看到原方案与废弃原因，避免误用。

### Decision 5: 撤销 c30b2b3 isatty 守卫

**选择**：`main.cpp:442` 改回 `config.session.enable_input_thread = true;`，移除 `#include <unistd.h>` 和 `isatty()` 调用。

**理由**：
- single-reader 模式（Decision 1）消除了 race
- pipe / redirect 场景（如 `echo "hi" | ./pdk_chat_demo`）也能正常工作：input thread 读 stdin EOF 后 `getline` 返回 false → input thread 退出；main loop 的 `pop_next_input` 收到 shutdown 信号 → 退出循环
- 测试套件（`tests/test_pdk_chat_demo_stdin_e2e.cpp`）用 fork+exec + pipe 注入 stdin，行为一致

**撤回 c30b2b3 的理由**：
- 该 commit 仅作为 "automation probing fix"，是临时 workaround
- 真正的根因是 race，single-reader 模式下不再需要 isatty 分支
- c30b2b3 的 commit message 可保留作为历史，但代码层面撤销

### Decision 6: 复用现有 stop_input_thread_ 作为 shutdown 信号

**选择**：复用 `Impl::stop_input_thread_` (chat_session.cpp:211) 同时承担 input thread 关闭 + main loop 唤醒两个职责。

**理由**：
- 避免引入新 atomic 字段
- `pop_next_input` 的 cv predicate 检测 `stop_input_thread_.load()` 即可唤醒等待中的 main loop
- ~析构函数已经设置 stop + join input thread，main loop 自然退出

### Decision 7: SINGLE SHARED CancellationRegistry Instance（Oracle C1 修复）

**选择**：进程内**只有一个** `CancellationRegistry` 实例，由 `ChatSession::Impl` 和 `pdk/loop_agent` 共享。**删除** `pdk/loop_agent/src/pdk_entry.cpp:148` 的 file-static `g_loop_registry`，改为通过 `command_globals.h` 风格的全局 `g_cancellation_registry` 共享。

**理由**：
- **Oracle C1 修复**：当前实现中 `ChatSession::Impl::cancellation_registry_` (chat_session.cpp:222,304) 与 `pdk/loop_agent::g_loop_registry` (pdk_entry.cpp:148) 是**两个独立实例**，导致：
  - ChatSession register source → `impl_->cancellation_registry_`
  - loop_agent resolve → `g_loop_registry` (恒为空，无 register 调用)
  - `resolve_token("")` 返回空 token → `pdk_entry.cpp:264 stop_requested()` 恒 false → LLM provider 收不到 cancel
- 测试 `test_chat_session_cancellation.cpp:72-77` 用同一实例模拟两端，掩盖生产环境双实例断链
- 必须改为**单一共享实例**才能让 register/resolve 两端引用同一 registry

**具体实现**：
```cpp
// 1. 新增 examples/pdk_chat_demo/commands/cancellation_globals.h
#pragma once
#include <memory>
#include "cancellation_registry.h"  // pdk_chat_demo namespace
namespace pdk_chat_demo {
extern std::shared_ptr<CancellationRegistry> g_cancellation_registry;
}

// 2. 新增 cancellation_globals.cpp
#include "cancellation_globals.h"
namespace pdk_chat_demo {
std::shared_ptr<CancellationRegistry> g_cancellation_registry;
}

// 3. main.cpp 在 ChatSession 构造前初始化共享 registry
#include "commands/cancellation_globals.h"
// 在 line 446 ChatSession 构造前：
pdk_chat_demo::g_cancellation_registry = std::make_shared<CancellationRegistry>();
pdk_chat_demo::ChatSession session(
    engine.get(), bus, &engine->get_tool_registry(),
    config.agent, config.session,
    pdk_chat_demo::g_cancellation_registry);  // 注入共享 registry

// 4. chat_session.cpp Impl 接受 shared_ptr<CancellationRegistry>（替代 self-owned）
// 构造签名变更：
Impl(..., std::shared_ptr<CancellationRegistry> registry)
    : cancellation_registry_(std::move(registry)) {}
// Impl::cancellation_registry_ 不再是 self-owned，而是引用共享实例

// 5. pdk/loop_agent/src/pdk_entry.cpp 删除 file-static g_loop_registry
// 替换为：
extern std::shared_ptr<pdk_chat_demo::CancellationRegistry>
    pdk_chat_demo::g_cancellation_registry;
// 在 loop/run handler 中：
auto token = pdk_chat_demo::g_cancellation_registry->resolve_token(cancellation_id);
```

**替代方案**：
- A. 通过 `loop_args["registry_ptr"]` 传递 registry 指针 — 增加序列化/反序列化复杂度，与现有 `bus_ptr` / `session_id` 透传模式不一致
- B. 把 CancellationRegistry 移入 loop_agent — 反向依赖（chat_session 依赖 loop_agent），违反现有架构
- C. **采纳**: 全局共享 registry，与现有 `g_command_session` / `g_command_coordinator` 模式一致

**Acceptance criteria**（与 tasks 4.0 对齐）：
- `grep "register_source\|static.*registry\|g_loop_registry" pdk/loop_agent/src/pdk_entry.cpp` 返回 0 处
- 新增跨组件 token identity 测试（M5.1 Extra#2）：ChatSession register → loop_agent resolve → token identity assert
- 现有 `test_chat_session_cancellation.cpp` 改造为经共享 registry 注入
- default-token 路径修复：`chat(input)` 默认重载也自建 stop_source 并注册（确保 cancellation_id 非空）
- ctest 全量零回归

**Effort**: 0.5-1 天（含 wiring + 测试改造 + ChatSession 构造签名变更）

## Risks / Trade-offs

| 风险 | 影响 | 缓解 |
|---|---|---|
| **R1: cv.wait_for 的 spurious wakeup** | main loop 偶发空唤醒，多消耗一次 try_pop 调用 | predicate 检查 shutdown OR pending_input_count_ > 0（atomic, 无 data race），spurious wakeup 自然 retry |
| **R2: interrupt_thread 与 chat() 退出的 join 时序** | chat() 早退（如 LLM 抛异常）时 interrupt_thread 未 join → std::terminate | RAII guard 在 chat() 入口构造 GuardChatScope，析构时无条件 stop + join interrupt_thread |
| **R3: pop_next_input 持锁期间 enqueue 被阻塞** | input thread 短暂等待 cv_mutex_（毫秒级） | input thread 仅持 steering_mutex_ 或 follow_up_mutex_ 之一，cv_mutex_ 极短持锁；enqueue 顺序 push → pending_count.fetch_add → notify_one（atomic 保证可见性） |
| **R4: follow-up drain 在 messages 中插入"非本轮"消息** | LLM history 包含用户已输但本轮未明示的消息（语义错位） | 设计取舍：drain 后 messages 包含完整用户输入序列，LLM 在下一轮看到（与"用户连续输入 3 轮"语义一致）；Spec Requirement 明确描述此行为 |
| **R5: pipe 模式下 input thread 与 main loop 退出时序** | pipe EOF 后 main loop 阻塞在 pop_next_input 不退出 | input thread 检测到 EOF 后设置 stop_input_thread_ 并退出；pop_next_input 的 cv predicate 检测 shutdown → 返回 nullopt → main loop break |
| **R6: isatty 守卫撤回后回退风险** | 用户已 merge c30b2b3 修复，可能不愿撤回 | proposal §Impact 明确说明"撤销而非修复"，design.md §Decision 5 解释根因（race 已通过 single-reader 消除） |
| **R7: interrupt_thread 启动开销** | 每轮 chat() 创建+销毁一个 std::thread（~10μs） | 100ms 轮询周期，10μs 启动开销 < 1% 性能影响，可接受 |
| **R8: 现有测试 test_chat_session_queues.cpp 是否兼容** | 测试 helper（try_push_*_for_test / queue_size / try_clear_queue）行为不变；新 API 不破坏 | 4 个测试保持 PASS（验证：dry-run ctest） |

## 迁移 / 回滚

**部署步骤**（与方案 A 兼容）：
1. 合并此 change 后，main.cpp 不再需要 isatty 检查，所有 stdin 模式行为一致
2. 不需要清空旧 session JSONL 文件（messages 字段兼容）
3. 不需要重新训练或重建 plugin（chat_session API 是 header-only 扩展）

**回滚策略**：
- git revert 此 change，恢复 c30b2b3 的 isatty 守卫
- 风险：消费者侧 API（`pop_next_input` / `try_pop_input`）已加入 header，若其他代码引用则编译失败；本 change 不引入外部调用方，回滚安全

**前置条件**：
- ✅ chat-async-queue-infra (d4fcca1) producer 已 ship
- ✅ chat-async-cancellation-chain (d1ecca2) stop_token 链路已 ship
- ✅ chat-async-io-model-switching (526c88b) /model 命令已 ship

**后置影响**：
- 解锁 Phase D (`chat-async-io-consumer-loop` 的能力 → TUI 多窗口 support 等)
- 验证 chat-async-io 系列 4 个 archived changes 的 spec 真正可工作（不是 dead producer）

## Open Questions

1. **Q1: interrupt_thread 的轮询周期 100ms 是否合适？**  
   - 选项 A: 100ms（用户感知 <500ms，CPU 开销 ~0.1%）  
   - 选项 B: 50ms（用户感知 <300ms，CPU 开销 ~0.2%）  
   - 选项 C: 200ms（用户感知 <800ms，CPU 开销 ~0.05%）  
   - **默认采纳 A**（100ms），如有反馈再调整；spec Requirement 写"≤500ms 内停止"，给实现自由度

2. **Q2: follow-up drain 是否要"批量合并"为单条消息？**  
   - 选项 A: 保留 3 条独立 user message（当前实现）— 与 chat history 一致  
   - 选项 B: 合并为 1 条 `"hello\nhow are you?\ngoodbye"` — 节省 LLM call 但语义奇怪  
   - **默认采纳 A**，与 chat-async-queue-infra spec 隐含语义一致

3. **Q3: 是否需要为 `/cancel` 之外的其他 steering 命令（如 `/model`）做 mid-turn interrupt？**  
   - 选项 A: 仅 `/cancel`（简单，Wave 3-A Phase C 已定义 `/model` 在 turn 完成后处理）  
   - 选项 B: 所有 `/` 开头都视为 mid-turn interrupt（复杂，需决策哪些安全）  
   - **默认采纳 A**，与 chat-async-io-model-switching spec 一致（"model switch at chat() entry"）
