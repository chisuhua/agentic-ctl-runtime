## ADDED Requirements

### Requirement: ChatSession exposes non-blocking input pop API

The `ChatSession` SHALL expose a non-blocking `try_pop_input()` method that returns the next pending input message from the steering or follow-up queue, with steering messages having higher priority.

The method SHALL return `std::optional<InputMessage>` where `InputMessage` is a struct with two fields:
- `kind`: enum value `Steering` or `FollowUp`
- `text`: the raw line content (command line for steering, message body for follow-up)

The method SHALL NOT block. If both queues are empty, it SHALL return `std::nullopt` immediately.

Priority ordering: steering messages SHALL be dequeued before follow-up messages. If a steering message exists, it SHALL be returned even if follow-up queue is non-empty.

#### Scenario: Steering message has priority over follow-up

- **WHEN** `try_pop_input()` is called
- **AND WHEN** both `steering_queue_` and `follow_up_queue_` contain at least one message
- **THEN** the returned `InputMessage.kind` SHALL be `Steering`
- **AND THEN** the steering queue size SHALL decrease by 1
- **AND THEN** the follow-up queue size SHALL remain unchanged

#### Scenario: Empty queues return nullopt immediately

- **WHEN** `try_pop_input()` is called
- **AND WHEN** both `steering_queue_` and `follow_up_queue_` are empty
- **THEN** the method SHALL return `std::nullopt`
- **AND THEN** the call SHALL complete in O(1) time without blocking

#### Scenario: Follow-up message returned when steering queue is empty

- **WHEN** `try_pop_input()` is called
- **AND WHEN** `steering_queue_` is empty
- **AND WHEN** `follow_up_queue_` contains at least one message
- **THEN** the returned `InputMessage.kind` SHALL be `FollowUp`
- **AND THEN** the follow-up queue size SHALL decrease by 1

### Requirement: ChatSession exposes blocking input pop with condition variable

The `ChatSession` SHALL expose a `pop_next_input(std::chrono::milliseconds timeout)` method that blocks until an input message becomes available, the timeout elapses, or shutdown is requested.

The method SHALL use `std::condition_variable` to efficiently wait (no busy-loop).

The method SHALL wake up early and return `std::nullopt` if shutdown is requested via `request_stop()` or destructor entry.

The method SHALL return `std::nullopt` if the timeout elapses without any input arriving.

#### Scenario: Block returns immediately when message already queued

- **WHEN** `pop_next_input(1000ms)` is called
- **AND WHEN** `follow_up_queue_` already contains a message (queued before the call)
- **THEN** the call SHALL return immediately with that message
- **AND THEN** the call SHALL complete in O(1) time

#### Scenario: Block wakes up when input arrives during wait

- **WHEN** `pop_next_input(5000ms)` is called
- **AND WHEN** both queues are empty at call time
- **AND WHEN** 200ms later the input thread enqueues a message
- **THEN** the call SHALL return within 50ms after enqueue
- **AND THEN** the returned message SHALL be the newly enqueued one

#### Scenario: Timeout returns nullopt when no input arrives

- **WHEN** `pop_next_input(100ms)` is called
- **AND WHEN** no message is enqueued within 100ms
- **THEN** the call SHALL return `std::nullopt` after approximately 100ms
- **AND THEN** no message SHALL be dequeued (queues remain unchanged)

#### Scenario: Shutdown wakes up blocking pop immediately

- **WHEN** `pop_next_input(5000ms)` is called
- **AND WHEN** 200ms later `request_stop()` is invoked (e.g., SIGINT handler)
- **THEN** the call SHALL return `std::nullopt` within 50ms after `request_stop()`
- **AND THEN** the destructor's input thread join SHALL NOT deadlock

### Requirement: Main loop is the single consumer of input queues

The `examples/pdk_chat_demo/main.cpp` main loop SHALL consume input exclusively through `session.pop_next_input(timeout)` and SHALL NOT call `std::getline(std::cin, ...)` directly.

The input thread (`ChatSession::Impl::input_thread_`) SHALL be the sole reader of `stdin`.

This single-reader pattern eliminates the race condition where main loop and input thread both call `std::getline` and one of them captures only a partial line.

#### Scenario: TTY mode runs without race

- **WHEN** the demo is launched in a TTY with stdin connected to a terminal
- **AND WHEN** the user types `what can you do?` followed by Enter
- **THEN** the entire string `what can you do?` SHALL appear in the `user.input` event payload
- **AND THEN** no character SHALL be lost (no truncation like `hat can you do?`)
- **AND THEN** `session.chat("what can you do?")` SHALL be called with the complete string

#### Scenario: Pipe mode runs without race

- **WHEN** the demo is launched with `echo "what can you do?" | ./pdk_chat_demo`
- **AND WHEN** stdin EOF is reached after one line
- **THEN** the entire string `what can you do?` SHALL be processed by `session.chat()`
- **AND THEN** the demo SHALL exit cleanly (not hang) after LLM response

#### Scenario: Sequential user inputs are all processed

- **WHEN** the user types `first message\n` then waits for response then types `second message\n`
- **THEN** both messages SHALL be processed in order
- **AND THEN** each message SHALL appear complete in the corresponding `user.input` event payload

### Requirement: Steering messages interrupt current turn via SHARED CancellationRegistry

The system SHALL route cancellation signals through a **SINGLE shared `CancellationRegistry` instance**, jointly owned by `ChatSession` and all agent loop entry points (notably `pdk/loop_agent`).

The system MUST satisfy all of the following:

- `ChatSession::chat(input, token)` SHALL register the turn's `stop_source` into the **shared registry** and SHALL pass the resulting `cancellation_id` to the loop entry point via `loop_args["cancellation_id"]`.
- `pdk/loop_agent` (in `pdk/loop_agent/src/pdk_entry.cpp`) SHALL resolve `cancellation_id` from the **same shared registry instance**. It MUST NOT instantiate a file-static local registry (e.g., a previous `g_loop_registry` is removed).
- The shared registry SHALL be constructed **exactly once per process** and SHALL be injected into both `ChatSession::Impl` and the loop_agent entry point (e.g., via a `command_globals.h`-style global pointer set during `main()` startup, mirroring the existing `g_command_session` pattern).
- During `ChatSession::chat()` execution, an interrupt poll thread SHALL call `try_peek_input()` periodically (at most every 100ms) to detect `/cancel` steering messages. The poll thread SHALL **pop the message ONLY when `/cancel` is at the head** (via `try_pop_input()`); non-`/cancel` messages MUST remain in the queue for main-loop processing (consistent with Req 5's single-consumer invariant and the "Non-cancel steering messages do not interrupt" scenario below). When `/cancel` is popped, it SHALL call `request_stop()` on the resolved source from the shared registry.

When `ChatSession::chat(input)` is called WITHOUT a token (the default overload used by `main.cpp:590`), the implementation SHALL **internally create or adopt a `stop_source`, register it into the shared registry**, and produce a non-empty `cancellation_id` such that cancellation remains deliverable. (An empty `cancellation_id` resulting in a no-op `request_stop` is a regression and is NOT acceptable.)

The interrupt detection (from enqueue to `request_stop()` invocation) SHALL complete within 500ms of the steering message being enqueued (including the 100ms poll period).

#### Scenario: /cancel during LLM call stops current turn (token overload)

- **WHEN** `session.chat("long task", token)` is in progress with a non-default token (LLM call blocking)
- **AND WHEN** 200ms after chat() entry, the user types `/cancel` and presses Enter
- **THEN** within 500ms total, the chat() call SHALL return with `success = false` and `error_message` containing "cancelled"
- **AND THEN** the `loop.done` event SHALL NOT be emitted (turn was cancelled before completion)
- **AND THEN** `loop.error` event SHALL be emitted with `error = "cancelled"`
- **AND THEN** the shared registry SHALL be the single instance consulted by both the interrupt thread and `pdk/loop_agent` (verifiable via token identity assertion across components)

#### Scenario: /cancel interrupts chat() called WITHOUT explicit token

- **WHEN** `session.chat("long task")` is called using the default overload (no token) — as in `main.cpp:590`
- **AND WHEN** 200ms after chat() entry, the user types `/cancel` and presses Enter
- **THEN** within 500ms total, the chat() call SHALL return with `success = false` and `error_message` containing "cancelled"
- **AND THEN** ChatSession SHALL have internally created a `stop_source`, registered it into the shared registry, and passed the non-empty `cancellation_id` via `loop_args`
- **AND THEN** the interrupt thread SHALL resolve the same `cancellation_id` from the shared registry and call `request_stop()`

#### Scenario: Cross-component token identity via shared registry

- **WHEN** ChatSession registers a `stop_source` and obtains a `cancellation_id`
- **AND WHEN** `pdk/loop_agent` resolves that same `cancellation_id` via the shared registry
- **THEN** the `stop_token` obtained by loop_agent SHALL refer to the **same `stop_source`** as the one ChatSession registered (token identity verified via `stop_token == stop_source.get_token()` check)
- **AND THEN** calling `request_stop()` on either end SHALL be observable by the other end within the same memory_order semantics (verified by both endpoints observing `stop_requested() == true`)

#### Scenario: Non-cancel steering messages do not interrupt

- **WHEN** `session.chat("task")` is in progress
- **AND WHEN** 200ms after chat() entry, the user types `/model openai`
- **THEN** the chat() call SHALL complete normally (not interrupted)
- **AND THEN** the `/model openai` steering message SHALL be processed in the next turn (via chat-async-io-model-switching existing logic)

#### Scenario: Interrupt polling stops after chat() returns

- **WHEN** `session.chat("task")` returns (whether success or cancelled)
- **THEN** no more `try_pop_input()` calls SHALL occur from the interrupt poll thread
- **AND THEN** the interrupt thread SHALL be joined before chat() returns (no dangling thread)

### Requirement: Follow-up messages are processed in order by the main loop

The `follow_up_queue_` SHALL be consumed by **exactly one consumer: the main loop** in `examples/pdk_chat_demo/main.cpp`. `ChatSession::chat()` SHALL NOT drain `follow_up_queue_` (to avoid double-consumption — see Oracle NC1 finding).

When the main loop pops a `Kind::FollowUp` message from `session.pop_next_input()`, it SHALL call `session.chat(msg.text)` to process it. Each follow-up message triggers exactly one `loop/run` call (preserving single-message-per-turn semantics from `chat-async-queue-infra`).

Processing SHALL preserve FIFO order.

The main loop SHALL NOT pop a follow-up message and silently drop it; every popped follow-up message MUST result in a `session.chat()` invocation.

The follow-up queue SHALL respect capacity: if `follow_up_queue_` exceeds capacity, the input thread's existing overflow rejection (per `chat-async-queue-infra` spec) prevents accumulation beyond 32 entries.

#### Scenario: Single follow-up message processed in next turn

- **WHEN** user types `what can you do?` then immediately types `how are you?`
- **AND WHEN** the first `session.chat("what can you do?")` returns
- **THEN** the main loop SHALL pop `how are you?` from `follow_up_queue_` and call `session.chat("how are you?")`
- **AND THEN** the user SHALL see two `Assistant:` responses in sequence (one per chat() call)
- **AND THEN** each message SHALL be processed exactly ONCE (no double-consumption)

#### Scenario: Multiple follow-up messages processed in order

- **WHEN** user types `msg1\n` then `msg2\n` then `msg3\n` rapidly (within one turn)
- **THEN** the main loop SHALL pop them in order and call `session.chat()` for each
- **AND THEN** the user SHALL see three `Assistant:` responses in sequence (one per message)

#### Scenario: Steering and follow-up messages interleaved

- **WHEN** user types `/model openai` then `hello` then `/cancel` then `goodbye` rapidly
- **THEN** the main loop SHALL pop and process them in order: `/model openai` (command), `hello` (chat), `/cancel` (registered command, handler calls `session.request_stop()` — no-op when `current_cancellation_id_` is empty, per chat_session.cpp:271), `goodbye` (chat)
- **AND THEN** exactly two `Assistant:` responses SHALL appear (for `hello` and `goodbye`)
- **AND THEN** `/cancel` SHALL NOT produce "unknown command" output (registered as a command per tasks §7.7)

#### Scenario: No double-consumption by chat()

- **WHEN** main loop pops a follow-up message and calls `session.chat(text)`
- **THEN** `chat()` SHALL NOT additionally drain `follow_up_queue_` (verified by single-shot processing per call)
- **AND THEN** no message SHALL appear twice in `impl_->messages` history (verified by history size == number of popped messages)

### Requirement: Input thread is unconditionally enabled (no isatty guard)

`SessionConfig::enable_input_thread` SHALL default to `true` (constructor initializer in `chat_session.h:42`).

`main.cpp` SHALL set `config.session.enable_input_thread = true;` unconditionally (no `isatty()` check, no `<unistd.h>` include).

The previous `isatty(STDIN_FILENO) != 0` workaround from `c30b2b3` SHALL be reverted because single-reader mode eliminates the race condition it was guarding against.

#### Scenario: enable_input_thread default is true

- **WHEN** `SessionConfig cfg;` is constructed without explicit field assignment
- **THEN** `cfg.enable_input_thread` SHALL be `true`
- **AND THEN** `ChatSession` constructor SHALL start the input thread

#### Scenario: TTY mode uses single-reader (input thread)

- **WHEN** the demo is launched in TTY
- **THEN** `enable_input_thread` SHALL be `true`
- **AND THEN** the input thread SHALL be the sole reader of stdin
- **AND THEN** main loop SHALL call `pop_next_input()` instead of `std::getline(std::cin, ...)`

#### Scenario: Pipe mode uses single-reader (input thread) without isatty branch

- **WHEN** the demo is launched with stdin redirected (`echo ... | ./pdk_chat_demo`)
- **THEN** `enable_input_thread` SHALL still be `true` (no isatty branch in main.cpp)
- **AND THEN** the input thread SHALL detect EOF and exit cleanly
- **AND THEN** main loop SHALL exit after `pop_next_input()` returns nullopt (shutdown signal)
