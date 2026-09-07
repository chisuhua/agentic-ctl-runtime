## 1. ChatSession API 扩展（InputMessage + try_pop_input + pop_next_input）

- [ ] 1.1 在 `chat_session.h` 新增 `InputMessage` 结构体（`Kind` 枚举 `Steering`/`FollowUp` + `text` 字段），位置紧邻 `QueueKind` 枚举
- [ ] 1.2 在 `ChatSession` public 接口新增 `std::optional<InputMessage> try_pop_input()` 方法（声明）
- [ ] 1.3 在 `ChatSession` public 接口新增 `std::optional<InputMessage> pop_next_input(std::chrono::milliseconds timeout)` 方法（声明）
- [ ] 1.4 在 `chat_session.h:42` 把 `SessionConfig::enable_input_thread` 默认值从 `false` 改为 `true`
- [ ] 1.5 在 `ChatSession::Impl` 新增字段：`std::condition_variable input_cv_` + `std::mutex input_cv_mutex_` + `std::atomic<size_t> pending_input_count_{0}`（声明，定义在 .cpp）
- [ ] 1.6 **NH1 修复 — 新增 `try_peek_input()` API**（声明）：在 `ChatSession` public 接口新增 `std::optional<InputMessage> try_peek_input() const` 方法（**仅查看队头不消费**，用于 interrupt poll 线程避免 NH1 消息丢失）

## 2. ChatSession 实现 try_pop_input / pop_next_input（Oracle C2/C3 修复）

- [ ] 2.1 在 `chat_session.cpp` 实现 `ChatSession::try_pop_input()` — 先 steering（持 steering_mutex_）再 follow-up（持 follow_up_mutex_），priority: steering；弹成功时 `pending_input_count_.fetch_sub(1)`
- [ ] 2.2 在 `chat_session.cpp` 实现 `ChatSession::pop_next_input(timeout)` — 进入前先 fast-path 检查 `pending_input_count_.load() > 0`；否则持 `input_cv_mutex_` + `input_cv_.wait_for()` + predicate `shutdown_ || pending_input_count_ > 0`（**不**读 queue.empty()，避免 Oracle C2 data race）
- [ ] 2.3 实现 `request_stop()` 唤醒 `pop_next_input` — 在 `request_stop()` 内调用 `input_cv_.notify_all()`（让所有阻塞的 pop 立即返回 nullopt）
- [ ] 2.4 复用 `Impl::stop_input_thread_` 作为 shutdown 信号 — 不新增 atomic 字段
- [ ] 2.5 **NC2 修复** — 修改 `try_push_steering_for_test()`（chat_session.cpp:598）：在 `steering_queue_.push(line)` 之后立即 `pending_input_count_.fetch_add(1, std::memory_order_release)`
- [ ] 2.6 **NC2 修复** — 修改 `try_push_follow_up_for_test()`（chat_session.cpp:608）：在 `follow_up_queue_.push(line)` 之后立即 `pending_input_count_.fetch_add(1, std::memory_order_release)`
- [ ] 2.7 **NC2 修复** — 修改 `try_clear_queue()`（chat_session.cpp:618）：clear 后用 `pending_input_count_.fetch_sub(N, std::memory_order_acq_rel)`（其中 N = 被 clear 的元素数）。**注意**（Oracle R3 修正）：禁止"重置为 0"备选——`pending_input_count_` 是 steering + follow-up 双队列共享计数，若另一队列非空则"重置为 0"会破坏不变量（后续 pop 路径 count==0 但实际 queue 非空 → fast-path 失效、predicate 恒假）。正确做法仅 `fetch_sub(N)`。
- [ ] 2.8 **NH1 修复** — 在 `chat_session.cpp` 实现 `ChatSession::try_peek_input() const`：先 steering（持 steering_mutex_）再 follow-up（持 follow_up_mutex_），仅返回 `front()` 的 text/Kind 拷贝，**不** `pop()`，**不** 修改 `pending_input_count_`；队列空时返回 `std::nullopt`

## 3. Input thread enqueue 通知（Oracle C3 修复）

- [ ] 3.1 修改 `chat_session.cpp::input_thread_main()` — 在 `steering_queue_.push(line)` 之后**先** `pending_input_count_.fetch_add(1)` **再** `input_cv_.notify_one()`（顺序保证 C3 不丢失唤醒）
- [ ] 3.2 同上 — 在 `follow_up_queue_.push(line)` 之后先 `pending_input_count_.fetch_add(1)` 再 `input_cv_.notify_one()`
- [ ] 3.3 验证 overflow 分支不 notify（避免 spurious wakeup）；count 与 push 同步（同 mutex scope）
- [ ] 3.4 **NH2 修复 — EOF shutdown**：修改 `input_thread_main()` 在 `std::getline(std::cin, line)` 返回 false（EOF）时，**先** `stop_input_thread_.store(true, std::memory_order_release)` **再** `input_cv_.notify_all()`（确保 main loop 在 pipe EOF 后能从 `pop_next_input` 返回 `std::nullopt` 并退出循环，否则 R5 风险"pipe 模式挂死"会真实发生）

## 4. Shared CancellationRegistry 共享（Oracle C1 修复 — BLOCKS 5.x）

**🚨 P0 任务。必须在本节所有 5.x 子任务开始前完成 4.0。**

- [ ] 4.0.1 在 `examples/pdk_chat_demo/commands/` 新建 `cancellation_globals.h` + `.cpp`，声明 `extern std::shared_ptr<CancellationRegistry> g_cancellation_registry`（与现有 `g_command_session` / `g_command_coordinator` 同模式）
- [ ] 4.0.2 修改 `ChatSession` 构造签名 — 新增 `std::shared_ptr<CancellationRegistry> registry` 参数（位置最后），`Impl::cancellation_registry_` 改为持有共享引用（不再 self-owned）
- [ ] 4.0.3 修改 `main.cpp` — 在 `ChatSession` 构造前 `pdk_chat_demo::g_cancellation_registry = std::make_shared<CancellationRegistry>();`，构造时传入
- [ ] 4.0.4 **删除** `pdk/loop_agent/src/pdk_entry.cpp:148` 的 file-static `g_loop_registry`，改为引用 `pdk_chat_demo::g_cancellation_registry`（loop/run handler 中 resolve_token）
- [ ] 4.0.5 修复 default-token 路径 — `ChatSession::chat(input)` 默认重载（chat_session.cpp:295）必须**自建 `stop_source`**、注册到共享 registry、返回**非空 `cancellation_id`**（当前 `token.stop_possible()==false` 分支保持空 id 是回归点）
- [ ] 4.0.6 新增跨组件 token identity 测试 `tests/test_chat_session_shared_registry.cpp` — ChatSession register → loop_agent resolve → token identity assert（暴露 C1 修复）
- [ ] 4.0.7 改造现有 `tests/test_chat_session_cancellation.cpp` — 改为经共享 registry 注入（不再各自 new CancellationRegistry），保持现有 5 个 test case 全部 PASS
- [ ] 4.0.9 **NC3 修复 — 构造签名默认值**：修改 `ChatSession` 构造函数，新参数 `std::shared_ptr<CancellationRegistry> registry = nullptr` 必须有默认值；Impl 构造时若 `registry == nullptr` 则 fallback 创建 self-owned（保持现有 26+ call site 不破坏）
- [ ] 4.0.10 **NH3 修复 — pdk_entry.cpp null-guard（Oracle R3 修正语义）**：在 `pdk/loop_agent/src/pdk_entry.cpp` 的 `loop/run` handler 中，`pdk_chat_demo::g_cancellation_registry->resolve_token(cancellation_id)` 之前加 nullptr 检查。**null 时降级为"non-cancellable-but-executable"**（与现有 `cancellation_id == ""` 的语义对齐，pdk_entry.cpp:245-247）：跳过 resolve、`cancellation_token` 保持默认空 token，继续执行 `loop/run`（mock fallback 或 real DSL），**不**返回 error。这是 §4.0.13 测试集中所有未初始化全局的测试二进制（test_e2e_mock、test_loop_agent_plugin 等）能够继续通过的前提。**禁止**返回 `success = false` 占位响应——那会让所有现有测试 FAIL。
- [ ] 4.0.11 **NH3 修复 — pdk/loop_agent CMake wiring**：修改 `pdk/loop_agent/CMakeLists.txt`，在 `target_include_directories` 中添加 `${CMAKE_SOURCE_DIR}/examples/pdk_chat_demo`，使 pdk_entry.cpp 可 include `cancellation_registry.h` / `cancellation_globals.h`；同时在 `target_link_libraries` 中链入 `pdk_chat_demo_obj`（或对应 INTERFACE 库）
- [ ] 4.0.12 **NC3 修复 — main.cpp:626 更新**：现有 `ChatSession discard(nullptr, nullptr, nullptr, {}, {})` (main.cpp:626) 5-arg 调用保持不变（依赖 4.0.9 默认值），但需在 PR description 明确列出此行为兼容性，并加注释说明 discard 实例使用默认 registry
- [ ] 4.0.13 **NH3 修复 — null-global deref 测试（Oracle R3 修正语义）**：新增 `tests/test_pdk_chat_demo_null_registry.cpp`，验证：当 `pdk_chat_demo::g_cancellation_registry == nullptr` 且 `cancellation_id != ""` 时，调 `loop/run` **不 crash** + **不返回 error** + **照常执行 loop**（non-cancellable-but-executable，与 empty cancellation_id 语义一致）。这保证 test_e2e_mock、test_loop_agent_plugin 等未初始化全局的测试二进制继续 PASS——它们依赖"调用 loop/run → 实际跑 LLM/mock → 返回结果"，而非"返回 cancellation_registry_uninitialized 错误"。
- [ ] 4.0.14 **AC 验收**：
  - ✅ `grep -rn "register_source\|static.*[Rr]egistry\|g_loop_registry" pdk/loop_agent/src/pdk_entry.cpp` 返回 0 处
  - ✅ 4.0.6 新增测试 PASS
  - ✅ 4.0.7 改造后 test_chat_session_cancellation 5/5 PASS
  - ✅ 4.0.13 新增 null-guard 测试 PASS
  - ✅ 9 个测试文件 26+ 个 ChatSession 构造点全部保持 5-arg 调用编译通过（依赖 4.0.9 默认值）
  - ✅ `cmake --build pdk/loop_agent` 成功（依赖 4.0.11 CMake wiring）
  - ✅ `ctest -R "chat_session|cancellation|pdk_chat_demo"` 全绿
  - ✅ 全量 `ctest -j$(nproc) --output-on-failure` 零回归（baseline 185/185）

## 5. Steering interrupt 中断当前 turn

**⚠️ 依赖：Task 4.0 已 ship（共享 CancellationRegistry 已就绪）。无 4.0 则 5.x ship 后 /cancel 永远无效。**

- [ ] 5.1 在 `ChatSession::chat()` 内 `loop/run` 调用前插入 `interrupt_thread` 启动逻辑（捕获 `cancellation_id` + `stop_poll_` atomic 字段）
- [ ] 5.2 实现 `interrupt_thread` 主循环（Oracle NH1/R3 收口版）— `sleep_for(100ms)` + **(a)** `try_peek_input()`（peek，**不消费**） + **(b)** 命中 `/cancel` 后才 `try_pop_input()` **消费该消息**（避免 main loop 后续 pop 时再次见到并打印 "unknown command"）+ **(c)** 从 `impl_->cancellation_registry_->resolve_source(id)->request_stop()`（**同源注册**而非全局，消除 H2 隐患；main.cpp 中两者巧合同对象）+ break
- [ ] 5.3 在 `ChatSession::Impl` 新增 `std::atomic<bool> stop_poll_{false}` 字段
- [ ] 5.4 实现 RAII GuardChatScope — 构造时初始化 stop_poll_ = false，析构时无条件 `stop_poll_.store(true)` + `if (interrupt_thread.joinable()) interrupt_thread.join()`
- [ ] 5.5 把 GuardChatScope 放在 `chat()` 入口第一行，确保异常路径也能 join（防 std::terminate）

**🚨 NH1 修复 (Oracle 二次审查)**: interrupt_thread 必须用 `try_peek_input()`（不消费消息），只有确认是 `/cancel` 才走 `try_pop_input()`。非 `/cancel` 消息（用户输入、其他命令）必须保留在 queue 中，由 main loop 或 chat() 处理（详见 NC1 修复：main loop 是 follow-up 唯一消费者）。

## 6. (已删除 — Oracle NC1 修复)

~~Follow-up queue 自动 drain~~ — **删除此节**。原 §6 与 main loop 的 `pop_next_input → chat(input)` 路径双重消费同一 follow-up queue，导致每条消息被处理两次（用户可见重复回复）。改由 main loop 唯一消费（见 tasks §7.5.2）。

## 7. main.cpp 改造（撤销 c30b2b3 isatty 守卫 + 改用 pop_next_input）

- [ ] 7.1 修改 `main.cpp:442` — `config.session.enable_input_thread = true;`（移除 `isatty(STDIN_FILENO) != 0` 调用）
- [ ] 7.2 检查 `#include <unistd.h>` 是否仅用于 isatty — 若是则移除；否则保留
- [ ] 7.3 重构 `main.cpp:546-607` 主循环 — 删除 `std::string input; while (std::getline(std::cin, input))`，改为 `while (auto msg = session.pop_next_input(500ms))`
- [ ] 7.4 处理 `pop_next_input` 返回 nullopt 的两种情况（timeout vs shutdown）— timeout 时 `continue` 重新等待 + 触发 shutdown 检查，shutdown 时 `break` 退出循环
- [ ] 7.5 拆分 `InputMessage.kind` 为 steering 与 follow-up 两种处理路径：
  - [ ] 7.5.1 `Steering` → 走原有 command 处理（command_registry.resolve_command）路径
  - [ ] 7.5.2 `FollowUp` → 走原有 `session.chat(input)` 路径
- [ ] 7.6 保留 `g_shutdown_requested` 检查（SIGINT/SIGTERM 触发的 async-signal-safe shutdown flag）
- [ ] 7.7 **H1 修复 — 注册 `/cancel` 命令**（Oracle R3）：新增 `examples/pdk_chat_demo/commands/cancel_command.cpp` + `.h`，定义 `make_cancel_command_spec()`，handler 调 `pdk_chat_demo::g_command_session->request_stop()`（无活动 turn 时 request_stop 内部 `if (current_cancellation_id_.empty()) return;` — 自然 no-op，无副作用）；在 main.cpp line ~523（command_registry.register_command 区域）添加 `command_registry.register_command(pdk_chat_demo::make_cancel_command_spec());`。**目的**：当 interrupt_thread 因 turn 极快/turn 之间未消费 `/cancel` 而漏掉时，main loop 通过 command 路径仍能触发 request_stop，并消除 "unknown command: /cancel" UX 缺陷。

## 8. 新增单元测试（test_chat_session_consumer.cpp）

- [ ] 8.1 新建 `examples/pdk_chat_demo/tests/test_chat_session_consumer.cpp`
- [ ] 8.2 测试 case 1: `try_pop_input priority: steering before follow-up` — 用 `try_push_steering_for_test` + `try_push_follow_up_for_test` 各 1 条，验证返回的是 steering
- [ ] 8.3 测试 case 2: `try_pop_input returns nullopt when empty` — 不 push 任何消息，调用 `try_pop_input()` 验证返回 nullopt
- [ ] 8.4 测试 case 3: `pop_next_input blocks until enqueue` — 创建 ChatSession（启动 input thread），`pop_next_input(2000ms)` 在另一线程同时调用 `try_push_follow_up_for_test`，验证返回时间和消息内容
- [ ] 8.5 测试 case 4: `pop_next_input timeout returns nullopt` — 不 push 消息，`pop_next_input(50ms)` 验证返回 nullopt 且耗时 ~50ms
- [ ] 8.6 测试 case 5 (新增 — 暴露 Oracle C2 修复): 1000 次并发 push + try_pop 循环，验证 `pending_input_count_` 同步正确，无丢失消息

## 9. 新增 E2E 测试（test_pdk_chat_demo_stdin_e2e.cpp）

- [ ] 9.1 新建 `examples/pdk_chat_demo/tests/test_pdk_chat_demo_stdin_e2e.cpp`
- [ ] 9.2 测试 case 1: `mock mode pipe e2e without race` — fork+exec `printf 'what can you do?\nexit\n' | pdk_chat_demo --mock`，验证 stdout 包含完整 `user.input: what can you do?`（无字符丢失）+ `Assistant:` 行
- [ ] 9.3 测试 case 2a (CI 友好 — 拆分 Metis Extra#1): `mock mode pipe with steering interrupt` — 用 `try_push_*_for_test` helper（Phase A 已有）在 ChatSession 构造后注入 `/cancel` 消息到 steering queue，验证 ≤500ms 内 turn 停止（替代原 8.3 的 `script -qc` TTY 模拟）
- [ ] 9.4 测试 case 3: `pipe eof triggers graceful exit` — fork+exec `echo "hi" | pdk_chat_demo`，验证进程在 ≤30s 内退出（不挂死）
- [ ] 9.5 测试 case 4 (新增 — 暴露 Oracle C1 修复): `cancellation propagates end-to-end` — ChatSession 注册 cancellation_id → 模拟 loop_agent entry 用相同 id resolve → request_stop → ChatSession 在 ≤500ms 内观察到 stop
- [ ] 9.6 在 `examples/pdk_chat_demo/tests/CMakeLists.txt` 注册 2 个新 test target

## 10. 验证与回归（Oracle C2 UB 暴露 + ship gate）

- [ ] 10.1 `cmake --build build --target pdk_chat_demo -j$(nproc)` 编译通过（0 error, 0 warning）
- [ ] 10.2 `ctest --output-on-failure -R "chat_session|pdk_chat_demo"` 全绿（baseline + 新增 case）
- [ ] 10.3 **新增 TSan preset 验证**（Oracle C2 暴露 — Metis Extra#4）— `cmake --preset tsan -DAGENTICDSL_BUILD_TESTS=ON && ctest -R chat_session_consumer --output-on-failure` 必须零 race warning
- [ ] 10.4 **新增 ASan preset 验证** — `cmake --preset asan -DAGENTICDSL_BUILD_TESTS=ON && ctest -R chat_session --output-on-failure` 必须零 leak/error
- [ ] 10.5 手动 TTY 验证 — `./pdk_chat_demo` 输入 `what can you do?`，验证日志 `user.input: what can you do?`（首字符 `w` 不再被吞）
- [ ] 10.6 手动 TTY 验证 — 同一会话连续输入 3 条消息（`hi`、`how are you`、`goodbye`），验证 3 条都正常处理（无第二轮卡死）
- [ ] 10.7 手动 pipe 验证 — `echo "what can you do?" | ./pdk_chat_demo --mock`，验证进程正常退出（不挂死）
- [ ] 10.8 手动 deepseek 真实 LLM 验证 — `./pdk_chat_demo` 输入问题，验证能看到 deepseek 真实回复（`Assistant:` 行非空）
- [ ] 10.9 手动 steering 中断验证（**requires Task 4.0 shipped** — Metis M7.1#3） — 在 MockBlockingProvider 风格的 LLM 慢响应期间输入 `/cancel`，验证 ≤500ms 内 turn 停止；前置条件：`grep -rn "register_source\|static.*[Rr]egistry" pdk/loop_agent/src/pdk_entry.cpp` 必须返回 0 行
- [ ] 10.10 检查 `examples/pdk_chat_demo/README.md` 是否需要更新（提及 single-reader 模式 + 共享 CancellationRegistry + 新测试）

## 11. OpenSpec 收尾

- [ ] 11.1 运行 `openspec validate chat-async-io-consumer-loop --strict` 验证所有 artifacts schema 合规
- [ ] 11.2 验证 `openspec status --change chat-async-io-consumer-loop` 显示 `isComplete: true`
- [ ] 11.3 提交 PR / merge 到 main（按 Single-Developer Mode 流程）
- [ ] 11.4 archive change（执行 `openspec archive chat-async-io-consumer-loop` 或 `/openspec-archive-change`）