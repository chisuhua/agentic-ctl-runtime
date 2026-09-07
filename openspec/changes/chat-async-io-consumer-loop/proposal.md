## Why

`chat-async-io-queue-infra` (d4fcca1, 2026-08-08) 已 ship 输入线程作为 producer 写入 `steering_queue_` 和 `follow_up_queue_`，但 consumer 从未 ship：main 循环仍然直接 `std::getline(std::cin, input)` 读 stdin，且 `ChatSession::chat()` 不消费任何队列。结果是双重 race — (a) main 线程与 input thread 同时从 stdin 读导致首字符被吞/行被截断；(b) input thread 写入的消息永远死在队列里无人消费。

现场复现（2026-09-07 15:54:38）：用户在 TTY 下输入 `what can you do?`，日志显示 `[15:54:38] user.input: hat can you do?`（`w` 被 input thread 抢走塞进 `follow_up_queue_`），main 线程读到残缺行 → `loop.done: total_steps=0, total_tokens=0` → 用户看到空回复。第二轮 `hi` 后 stdin buffer 状态错乱，main 线程 `getline` 阻塞导致程序挂起。

`c30b2b3` (2026-09-07 15:27:50) 的 `isatty(STDIN_FILENO) != 0` 修复**只解决 pipe 模式**（是自动化探测场景），TTY 模式下 `enable_input_thread = true` 仍启动 input thread，仍 race。这是一个 partial fix — 必须闭合 consumer loop 才能根本解决。

## What Changes

- **修复 race condition**：main 循环改为只从 `follow_up_queue_` / `steering_queue_` 消费（不在 main 线程直接读 stdin），让 input thread 成为唯一 stdin reader
- **添加 queue consumer API**：`ChatSession::try_pop_steering()` / `try_pop_follow_up()` 返回 `optional<string>`，非阻塞、原子；main 循环使用 condition_variable 实现可中断的阻塞等待（避免 busy-loop）
- **闭合 producer-consumer 契约**：input thread 启动条件从 `enable_input_thread = isatty(...) != 0` 改为 `enable_input_thread = true`（无条件启用，TYY/pipe 都用 input thread）；同时移除 main 线程的 `std::getline(std::cin, ...)` 调用
- **扩展 c30b2b3 范围**：撤销 `isatty` 守卫（不再需要），因为 race 已通过 single-reader 模式消除
- **新增 steering 中断机制**：`chat()` 执行期间定期 poll `steering_queue_`，发现 `/cancel` 立即中断当前 turn 并返回 partial result（与 `chat-async-cancellation-chain` 的 stop_token 链路协同）
- **follow-up 队列在 turn 完成后自动 drain**：避免用户连续输入多轮时消息堆积导致 overflow
- **Non-breaking for external API**：所有新增 API（`try_pop_*`）为新方法，不修改现有签名；`enable_input_thread` 字段保留但语义改为"是否启动 input thread（推荐 true）"，main.cpp 始终传 true
- **新增 4 个 E2E 测试**：覆盖 single-reader mode 无 race / steering interrupt during turn / follow-up drain after turn / pipe mode (stdin redirect) end-to-end

## Capabilities

### New Capabilities

- `chat-async-io-consumer-loop`: 闭合 chat-async-io 系列的 producer-consumer 闭环。定义 main loop 作为唯一 consumer 的契约、queue pop API（`try_pop_steering` / `try_pop_follow_up`）、steering 中断机制（`/cancel` 立即停当前 turn）、follow-up 自动 drain、condition_variable 阻塞等待语义、E2E 验证场景（4 个 scenarios）。

### Modified Capabilities

- `chat-async-queue-infra`: 原始 spec 写"Input thread produces, ChatSession consumes"，但 consumer side 从未 ship。现把 consumer contract 转移到新 spec `chat-async-io-consumer-loop`（prose 引用），原 spec 保留 producer 行为不变（queue 字段、capacity=32、分类规则、测试 helper 全部已 ship）。**本 change 不修改 `chat-async-queue-infra` spec.md 的 Requirements**，因为 consumer 行为是新 spec 的新增领域 — 避免与已 archive 的 d4fcca1 冲突。

- `pdk-chat-demo-runtime-fix`: 现有 spec 只跟踪 provider/resolve 死锁等 runtime 问题。本次新增"single-reader mode"作为一个新 Requirement 加入 — `main loop MUST NOT call std::getline(std::cin, ...)`、`enable_input_thread MUST be true unconditionally`、TTY/pipe 双模式都用 input thread single-reader。

## Impact

**受影响的代码**：
- `examples/pdk_chat_demo/main.cpp` (line 547)：移除 `while (std::getline(std::cin, input))`，改为基于 `session.pop_next_input()` + condition_variable 的循环
- `examples/pdk_chat_demo/main.cpp` (line 442)：简化 `config.session.enable_input_thread = true;`（不再需要 isatty 守卫）
- `examples/pdk_chat_demo/chat_session.h` + `.cpp`：新增 `try_pop_steering()` / `try_pop_follow_up()` / `wait_for_input(timeout_ms)` API
- `examples/pdk_chat_demo/chat_session.cpp::chat()`：turn 入口处理 follow_up queue drain，turn 中断时 poll steering queue

**API 变更**：纯新增，不破坏现有签名。
- 新增：`bool try_pop_steering(string& out)` / `bool try_pop_follow_up(string& out)` / `void wait_for_input(chrono::milliseconds timeout)`
- 修改语义：`enable_input_thread` 字段保留，默认值从 `false` 改为 `true`（chat_session.h:42），main.cpp 不再需要手动设

**测试影响**：
- `tests/test_chat_session_queues.cpp`：现有 4 测试保持兼容（仅测试 helper）
- 新增 `tests/test_chat_session_consumer.cpp`：覆盖 4 scenarios（single-reader mode / steering interrupt / follow-up drain / pipe mode E2E）
- 新增 `tests/test_pdk_chat_demo_stdin_e2e.cpp`：fork+exec 子进程验证 stdin redirect 场景（替代 c30b2b3 的 pipe-mode workaround）

**依赖关系**：
- 前置依赖：`chat-async-queue-infra` (d4fcca1) producer 已 ship
- 协同：`chat-async-cancellation-chain` 的 stop_token 链路 — steering 中断机制复用 `CancellationRegistry`
- 协同：`chat-async-io-model-switching` (526c88b) 的 `/model` command 走 steering_queue_

**Non-goals**（明确范围边界）：
- ❌ 不修改 input thread 内部 producer 逻辑（d4fcca1 已 ship，不在本 change scope）
- ❌ 不修改 stop_token 链路本身（chat-async-cancellation-chain 已 ship）
- ❌ 不修改 `/model` 命令的 Wave 3-A Phase C 实现（chat-async-io-model-switching 已 ship）
- ❌ 不涉及 main loop 之外的 chat 入口（如 TUI 模式，独立的 pdk-chat-demo-tui 子项目）
- ❌ 不修复 c30b2b3 的 isatty 守卫逻辑（撤掉而非修复，因为 root cause 是 race，single-reader 模式消除 race 后 isatty 检查不再需要）

## 验证标准

- `cmake --build build --target pdk_chat_demo -j$(nproc)` 编译通过
- `ctest --output-on-failure -R "pdk_chat_demo|chat_session"` 全绿（baseline + 新增 4 cases）
- 手动验证（TTY 模式）：`./pdk_chat_demo`，输入 `what can you do?`，日志应显示 `user.input: what can you do?`（无字符丢失），`Assistant:` 后出现真实 deepseek 回复
- 手动验证（pipe 模式）：`echo "what can you do?" | ./pdk_chat_demo` 应正常单轮返回后退出（不卡死）
- 手动验证（steering interrupt）：在 `loop.run` 期间（LLM 调用慢响应）输入 `/cancel`，应在 ≤500ms 内停止当前 turn 并返回 partial result
- 现场复现验证：用户原报告的"首字符被吞 + 第二轮卡住"两个症状全部消失
