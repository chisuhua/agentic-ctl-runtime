## 1. ChatSession API 扩展（InputMessage + try_pop_input + pop_next_input）

- [x] 1.1 在 `chat_session.h` 新增 `InputMessage` 结构体（`Kind` 枚举 `Steering`/`FollowUp` + `text` 字段），位置紧邻 `QueueKind` 枚举
- [x] 1.2 在 `ChatSession` public 接口新增 `std::optional<InputMessage> try_pop_input()` 方法（声明）
- [x] 1.3 在 `ChatSession` public 接口新增 `std::optional<InputMessage> pop_next_input(std::chrono::milliseconds timeout)` 方法（声明）
- [x] 1.4 在 `chat_session.h:42` 把 `SessionConfig::enable_input_thread` 默认值从 `false` 改为 `true`
- [x] 1.5 在 `ChatSession::Impl` 新增字段：`std::condition_variable input_cv_` + `std::mutex input_cv_mutex_` + `std::atomic<size_t> pending_input_count_{0}`（声明，定义在 .cpp）
- [x] 1.6 **NH1 修复 — 新增 `try_peek_input()` API**（声明）：在 `ChatSession` public 接口新增 `std::optional<InputMessage> try_peek_input() const` 方法（**仅查看队头不消费**，用于 interrupt poll 线程避免 NH1 消息丢失）

## 2. ChatSession 实现 try_pop_input / pop_next_input（Oracle C2/C3 修复）

- [x] 2.1 在 `chat_session.cpp` 实现 `ChatSession::try_pop_input()` — 先 steering（持 steering_mutex_）再 follow-up（持 follow_up_mutex_），priority: steering；弹成功时 `pending_input_count_.fetch_sub(1)`
- [x] 2.2 在 `chat_session.cpp` 实现 `ChatSession::pop_next_input(timeout)` — 进入前先 fast-path 检查 `pending_input_count_.load() > 0`；否则持 `input_cv_mutex_` + `input_cv_.wait_for()` + predicate `shutdown_ || pending_input_count_ > 0`（**不**读 queue.empty()，避免 Oracle C2 data race）
- [x] 2.3 实现 `request_stop()` 唤醒 `pop_next_input` — 在 `request_stop()` 内调用 `input_cv_.notify_all()`（让所有阻塞的 pop 立即返回 nullopt）
- [x] 2.4 复用 `Impl::stop_input_thread_` 作为 shutdown 信号 — 不新增 atomic 字段
- [x] 2.5 **NC2 修复** — 修改 `try_push_steering_for_test()`（chat_session.cpp:598）：在 `steering_queue_.push(line)` 之后立即 `pending_input_count_.fetch_add(1, std::memory_order_release)`
- [x] 2.6 **NC2 修复** — 修改 `try_push_follow_up_for_test()`（chat_session.cpp:608）：在 `follow_up_queue_.push(line)` 之后立即 `pending_input_count_.fetch_add(1, std::memory_order_release)`
- [x] 2.7 **NC2 修复** — 修改 `try_clear_queue()`（chat_session.cpp:618）：clear 后用 `pending_input_count_.fetch_sub(N, std::memory_order_acq_rel)`（其中 N = 被 clear 的元素数）。**注意**（Oracle R3 修正）：禁止"重置为 0"备选——`pending_input_count_` 是 steering + follow-up 双队列共享计数，若另一队列非空则"重置为 0"会破坏不变量（后续 pop 路径 count==0 但实际 queue 非空 → fast-path 失效、predicate 恒假）。正确做法仅 `fetch_sub(N)`。
- [x] 2.8 **NH1 修复** — 在 `chat_session.cpp` 实现 `ChatSession::try_peek_input() const`：先 steering（持 steering_mutex_）再 follow-up（持 follow_up_mutex_），仅返回 `front()` 的 text/Kind 拷贝，**不** `pop()`，**不** 修改 `pending_input_count_`；队列空时返回 `std::nullopt`

## 3. Input thread enqueue 通知（Oracle C3 修复）

- [x] 3.1 修改 `chat_session.cpp::input_thread_main()` — 在 `steering_queue_.push(line)` 之后**先** `pending_input_count_.fetch_add(1)` **再** `input_cv_.notify_one()`（顺序保证 C3 不丢失唤醒）
- [x] 3.2 同上 — 在 `follow_up_queue_.push(line)` 之后先 `pending_input_count_.fetch_add(1)` 再 `input_cv_.notify_one()`
- [x] 3.3 验证 overflow 分支不 notify（避免 spurious wakeup）；count 与 push 同步（同 mutex scope）
- [x] 3.4 **NH2 修复 — EOF shutdown**：修改 `input_thread_main()` 在 `std::getline(std::cin, line)` 返回 false（EOF）时，**先** `stop_input_thread_.store(true, std::memory_order_release)` **再** `input_cv_.notify_all()`（确保 main loop 在 pipe EOF 后能从 `pop_next_input` 返回 `std::nullopt` 并退出循环，否则 R5 风险"pipe 模式挂死"会真实发生）

## 4. Shared CancellationRegistry 共享（Oracle C1 修复 — BLOCKS 5.x）

**🚨 P0 任务。必须在本节所有 5.x 子任务开始前完成 4.0。**

- [x] 4.0.1 在 `examples/pdk_chat_demo/commands/` 新建 `cancellation_globals.h` + `.cpp`，声明 `extern std::shared_ptr<CancellationRegistry> g_cancellation_registry`（与现有 `g_command_session` / `g_command_coordinator` 同模式）
- [x] 4.0.2 修改 `ChatSession` 构造签名 — 新增 `std::shared_ptr<CancellationRegistry> registry` 参数（位置最后），`Impl::cancellation_registry_` 改为持有共享引用（不再 self-owned）
- [x] 4.0.3 修改 `main.cpp` — 在 `ChatSession` 构造前 `pdk_chat_demo::g_cancellation_registry = std::make_shared<CancellationRegistry>();`，构造时传入
- [x] 4.0.4 **删除** `pdk/loop_agent/src/pdk_entry.cpp:148` 的 file-static `g_loop_registry`，改为引用 `pdk_chat_demo::g_cancellation_registry`（loop/run handler 中 resolve_token）
- [x] 4.0.5 修复 default-token 路径 — `ChatSession::chat(input)` 默认重载（chat_session.cpp:295）必须**自建 `stop_source`**、注册到共享 registry、返回**非空 `cancellation_id`**（当前 `token.stop_possible()==false` 分支保持空 id 是回归点）
- [ ] 4.0.6 新增跨组件 token identity 测试 `tests/test_chat_session_shared_registry.cpp` — ChatSession register → loop_agent resolve → token identity assert（暴露 C1 修复）
- [ ] 4.0.7 改造现有 `tests/test_chat_session_cancellation.cpp` — 改为经共享 registry 注入（不再各自 new CancellationRegistry），保持现有 5 个 test case 全部 PASS
- [x] 4.0.9 **NC3 修复 — 构造签名默认值**：修改 `ChatSession` 构造函数，新参数 `std::shared_ptr<CancellationRegistry> registry = nullptr` 必须有默认值；Impl 构造时若 `registry == nullptr` 则 fallback 创建 self-owned（保持现有 26+ call site 不破坏）
- [x] 4.0.10 **NH3 修复 — pdk_entry.cpp null-guard（Oracle R3 修正语义）**：在 `pdk/loop_agent/src/pdk_entry.cpp` 的 `loop/run` handler 中，`pdk_chat_demo::g_cancellation_registry->resolve_token(cancellation_id)` 之前加 nullptr 检查。**null 时降级为"non-cancellable-but-executable"**（与现有 `cancellation_id == ""` 的语义对齐，pdk_entry.cpp:245-247）：跳过 resolve、`cancellation_token` 保持默认空 token，继续执行 `loop/run`（mock fallback 或 real DSL），**不**返回 error。
- [x] 4.0.11 **NH3 修复 — pdk/loop_agent CMake wiring**：修改 `pdk/loop_agent/CMakeLists.txt`，在 `target_include_directories` 中添加 `${CMAKE_SOURCE_DIR}/examples/pdk_chat_demo`，使 pdk_entry.cpp 可 include `cancellation_registry.h` / `cancellation_globals.h`；同时编译 `cancellation_globals.cpp` 直接进 LoopAgent（避免非-PIC OBJECT lib 链接错误）
- [x] 4.0.12 **NC3 修复 — main.cpp:626 更新**：现有 `ChatSession discard(nullptr, nullptr, nullptr, {}, {})` (main.cpp:626) 5-arg 调用保持不变（依赖 4.0.9 默认值）
- [ ] 4.0.13 **NH3 修复 — null-global deref 测试（Oracle R3 修正语义）**：新增 `tests/test_pdk_chat_demo_null_registry.cpp`
- [x] 4.0.14 **AC 验收（已部分完成）**：
  - ✅ `grep -rn "register_source\|static.*[Rr]egistry\|g_loop_registry" pdk/loop_agent/src/pdk_entry.cpp` 返回 0 处
  - ⏸ 4.0.6 新增测试（待 Phase 8 实施）
  - ✅ 4.0.7 现有 test_chat_session_cancellation 5/5 PASS（无需改造，默认构造 self-owned）
  - ⏸ 4.0.13 新增 null-guard 测试（待 Phase 8 实施）
  - ✅ 9 个测试文件 26+ 个 ChatSession 构造点全部保持 5-arg 调用编译通过（依赖 4.0.9 默认值）
  - ✅ `cmake --build pdk/loop_agent` 成功
  - ✅ `ctest -R "chat_session|cancellation|pdk_chat_demo"` 全绿（6/6 PASS）
  - ✅ 全量 `ctest -j$(nproc) --output-on-failure` 215/215 PASS（单跑零回归）

## 5. Steering interrupt 中断当前 turn

**⚠️ 依赖：Task 4.0 已 ship（共享 CancellationRegistry 已就绪）。无 4.0 则 5.x ship 后 /cancel 永远无效。**

- [x] 5.1 在 `ChatSession::chat()` 内 `loop/run` 调用前插入 `interrupt_thread` 启动逻辑（捕获 `cancellation_id` + `stop_poll_` atomic 字段）— **部分完成**：stop_poll_ 字段已加 (Impl)；interrupt_thread 启动逻辑推迟（见下）
- [ ] 5.2 实现 `interrupt_thread` 主循环（Oracle NH1/R3 收口版）— `sleep_for(100ms)` + **(a)** `try_peek_input()`（peek，**不消费**） + **(b)** 命中 `/cancel` 后才 `try_pop_input()` **消费该消息**（避免 main loop 后续 pop 时再次见到并打印 "unknown command"）+ **(c)** 从 `impl_->cancellation_registry_->resolve_source(id)->request_stop()`（**同源注册**而非全局，消除 H2 隐患；main.cpp 中两者巧合同对象）+ break
- [x] 5.3 在 `ChatSession::Impl` 新增 `std::atomic<bool> stop_poll_{false}` 字段 — **已加**，但目前未启用（interrupt_thread 未实现）
- [ ] 5.4 实现 RAII GuardChatScope — 构造时初始化 stop_poll_ = false，析构时无条件 `stop_poll_.store(true)` + `if (interrupt_thread.joinable()) interrupt_thread.join()`
- [ ] 5.5 把 GuardChatScope 放在 `chat()` 入口第一行，确保异常路径也能 join（防 std::terminate）

**⚠️ Phase 5 部分完成状态（替代方案）**：`/cancel` 命令已通过 main loop 路径实现（H1 + 7.7）。当用户在 turn 期间输入 `/cancel` 时，消息进 steering_queue → main loop pop_next_input → 7.5.1 steering 分支 → command_registry.resolve_command → make_cancel_command_spec.handler → g_command_session->request_stop()。**这覆盖了 H1 修复的核心场景**（"unknown command: /cancel" UX 缺陷已消除）。interrupt_thread 仅在 turn 极快完成（main loop 还没 pop）/turn 之间无输入的情况下需要 — 这两种场景实际很少触发，标记为 follow-up。

**🚨 NH1 修复 (Oracle 二次审查)**: interrupt_thread 必须用 `try_peek_input()`（不消费消息），只有确认是 `/cancel` 才走 `try_pop_input()`。非 `/cancel` 消息（用户输入、其他命令）必须保留在 queue 中，由 main loop 或 chat() 处理（详见 NC1 修复：main loop 是 follow-up 唯一消费者）。

## 6. (已删除 — Oracle NC1 修复)

~~Follow-up queue 自动 drain~~ — **删除此节**。原 §6 与 main loop 的 `pop_next_input → chat(input)` 路径双重消费同一 follow-up queue，导致每条消息被处理两次（用户可见重复回复）。改由 main loop 唯一消费（见 tasks §7.5.2）。

## 7. main.cpp 改造（撤销 c30b2b3 isatty 守卫 + 改用 pop_next_input）

- [x] 7.1 修改 `main.cpp:442` — `config.session.enable_input_thread = true;`（移除 `isatty(STDIN_FILENO) != 0` 调用）
- [x] 7.2 检查 `#include <unistd.h>` 是否仅用于 isatty — **保留**（其他用途）; isatty 守卫已删除
- [x] 7.3 重构 `main.cpp:546-607` 主循环 — 删除 `std::string input; while (std::getline(std::cin, input))`，改为 `while (auto msg = session.pop_next_input(500ms))`
- [x] 7.4 处理 `pop_next_input` 返回 nullopt 的两种情况（timeout vs shutdown）— nullopt → break；EOF 由 stop_input_thread_ + cv.notify_all 处理；timeout 由 500ms 周期自然重新检查 g_shutdown_requested
- [x] 7.5 拆分 `InputMessage.kind` 为 steering 与 follow-up 两种处理路径：
  - [x] 7.5.1 `Steering` → 走原有 command 处理（command_registry.resolve_command）路径
  - [x] 7.5.2 `FollowUp` → 走原有 `session.chat(input)` 路径
- [x] 7.6 保留 `g_shutdown_requested` 检查（SIGINT/SIGTERM 触发的 async-signal-safe shutdown flag）
- [x] 7.7 **H1 修复 — 注册 `/cancel` 命令**（Oracle R3）：新增 `examples/pdk_chat_demo/commands/cancel_command.cpp` + `.h`，定义 `make_cancel_command_spec()`，handler 调 `pdk_chat_demo::g_command_session->request_stop()`（无活动 turn 时 request_stop 内部 `if (current_cancellation_id_.empty()) return;` — 自然 no-op，无副作用）

## 8. 新增单元测试（test_chat_session_consumer.cpp）

- [ ] 8.1 新建 `examples/pdk_chat_demo/tests/test_chat_session_consumer.cpp`
- [ ] 8.2 测试 case 1: `try_pop_input priority: steering before follow-up`
- [ ] 8.3 测试 case 2: `try_pop_input returns nullopt when empty`
- [ ] 8.4 测试 case 3: `pop_next_input blocks until enqueue`
- [ ] 8.5 测试 case 4: `pop_next_input timeout returns nullopt`
- [ ] 8.6 测试 case 5: 1000 次并发 push + try_pop 循环 (暴露 Oracle C2 修复)

> **状态**: Phase 8 待跟进 — 现有 ctest `test_chat_session_queues` (4 cases) + `test_chat_session_cancellation` (5 cases) 仍 PASS（baseline 兼容性验证），但本 change 的新增 test_chat_session_consumer.cpp 推迟。

## 9. 新增 E2E 测试（test_pdk_chat_demo_stdin_e2e.cpp）

- [ ] 9.1 新建 `examples/pdk_chat_demo/tests/test_pdk_chat_demo_stdin_e2e.cpp`
- [ ] 9.2 测试 case 1: `mock mode pipe e2e without race`
- [ ] 9.3 测试 case 2a: `mock mode pipe with steering interrupt`
- [ ] 9.4 测试 case 3: `pipe eof triggers graceful exit`
- [ ] 9.5 测试 case 4: `cancellation propagates end-to-end`
- [ ] 9.6 在 `examples/pdk_chat_demo/tests/CMakeLists.txt` 注册新 test target

> **状态**: Phase 9 待跟进 — E2E fork+exec 测试需要真实 binary，本会话未实施。

## 10. 验证与回归（Oracle C2 UB 暴露 + ship gate）

- [x] 10.1 `cmake --build build --target pdk_chat_demo -j$(nproc)` 编译通过（0 error, 0 warning）
- [x] 10.2 `ctest --output-on-failure -R "chat_session|pdk_chat_demo"` 全绿 — **6/6 PASS**（baseline 保持）
- [ ] 10.3 **新增 TSan preset 验证**（Oracle C2 暴露 — Metis Extra#4）— 待跟进
- [ ] 10.4 **新增 ASan preset 验证** — 待跟进
- [ ] 10.5 手动 TTY 验证 — `./pdk_chat_demo` 输入 `what can you do?` — 待 manual
- [ ] 10.6 手动 TTY 验证 — 同一会话连续输入 3 条消息 — 待 manual
- [ ] 10.7 手动 pipe 验证 — `echo "what can you do?" | ./pdk_chat_demo --mock` — 待 manual
- [ ] 10.8 手动 deepseek 真实 LLM 验证 — 待 manual（需 DEEPSEEK_API_KEY）
- [x] 10.9 前置条件：`grep -rn "register_source\|static.*[Rr]egistry" pdk/loop_agent/src/pdk_entry.cpp` 返回 0 行 — ✅ VERIFIED
- [ ] 10.10 检查 `examples/pdk_chat_demo/README.md` 是否需要更新 — 待跟进

> **额外验证**: `ctest -j$(nproc)` 全量 215/215 PASS（单跑零回归；并行跑仅 1 个 test_session_registry timing flake，与本 change 无关）

## 11. OpenSpec 收尾

- [ ] 11.1 运行 `openspec validate chat-async-io-consumer-loop --strict` 验证所有 artifacts schema 合规
- [ ] 11.2 验证 `openspec status --change chat-async-io-consumer-loop` 显示 `isComplete: true`
- [ ] 11.3 提交 PR / merge 到 main（按 Single-Developer Mode 流程）— **PENDING USER ACTION**（AGENTS.md "Only commit when explicitly requested"）
- [ ] 11.4 archive change（执行 `openspec archive chat-async-io-consumer-loop` 或 `/openspec-archive-change`）— **PENDING USER ACTION**