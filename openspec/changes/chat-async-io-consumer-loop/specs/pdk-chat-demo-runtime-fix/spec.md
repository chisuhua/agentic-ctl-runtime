## ADDED Requirements

### Requirement: pdk_chat_demo runs in single-reader mode

`pdk_chat_demo` main loop SHALL consume input exclusively through `ChatSession::pop_next_input()` and SHALL NOT call `std::getline(std::cin, ...)` directly. The `ChatSession` input thread SHALL be the sole reader of stdin.

This single-reader pattern eliminates the race condition where main loop and input thread both call `std::getline` and one of them captures only a partial line (e.g., user types `what can you do?` but main loop receives only `hat can you do?`).

The `SessionConfig::enable_input_thread` field SHALL default to `true` (no `isatty()` check, no `<unistd.h>` include in `main.cpp`).

This requirement supersedes the partial fix from commit `c30b2b3` (`isatty(STDIN_FILENO) != 0`), which only addressed pipe mode but left TTY mode still racy.

#### Scenario: Mock mode end-to-end without race

- **WHEN** user runs `printf 'what can you do?\nexit\n' | ./pdk_chat_demo --mock`
- **THEN** the entire string `what can you do?` SHALL appear in the `user.input` event payload (no character loss)
- **AND THEN** `Assistant:` line SHALL appear with non-empty mock response
- **AND THEN** `[steps=0, tokens=0, cost=$0]` SHALL NOT appear (at least 1 loop step executed)
- **AND THEN** the demo SHALL exit cleanly after processing the single input

#### Scenario: TTY mode end-to-end without race

- **WHEN** user runs `./pdk_chat_demo --mock` in a TTY
- **AND WHEN** user types `Write a hello world in C++` followed by Enter
- **THEN** the entire string `Write a hello world in C++` SHALL appear in the `user.input` event payload
- **AND THEN** `Assistant:` line SHALL appear with mock C++ code response
- **AND THEN** `loop.done` event SHALL report `total_steps >= 1` and `total_tokens > 0`

#### Scenario: Sequential user inputs are all processed in TTY

- **WHEN** user runs `./pdk_chat_demo --mock` in a TTY
- **AND WHEN** user types `first message` then waits for response then types `second message`
- **THEN** both messages SHALL be processed in order
- **AND THEN** no message SHALL have characters lost (each `user.input` event has complete input)

### Requirement: pdk_chat_demo removes isatty guard from main.cpp

`examples/pdk_chat_demo/main.cpp` line ~442 SHALL set `config.session.enable_input_thread = true;` directly, without `isatty(STDIN_FILENO)` check.

The `#include <unistd.h>` line SHALL be removed if it was added solely for the isatty check (keep only if used elsewhere in `main.cpp`).

Both TTY mode and pipe/redirect mode SHALL use the same single-reader code path (no branching).

#### Scenario: main.cpp has no isatty call

- **WHEN** `examples/pdk_chat_demo/main.cpp` is read
- **THEN** the string `isatty` SHALL NOT appear in the file
- **AND THEN** the string `STDIN_FILENO` SHALL NOT appear in the file
- **AND THEN** `config.session.enable_input_thread` SHALL be assigned `true` directly

#### Scenario: Same code path handles TTY and pipe modes

- **WHEN** the demo runs in TTY
- **AND WHEN** the demo runs with stdin redirected (e.g., `echo "hi" | ./pdk_chat_demo`)
- **THEN** both modes SHALL execute the same `pop_next_input()` loop in main
- **AND THEN** the only difference SHALL be that pipe mode triggers EOF → shutdown, TTY mode continues reading
