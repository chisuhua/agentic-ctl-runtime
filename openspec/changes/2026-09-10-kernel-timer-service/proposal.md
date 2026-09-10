# Proposal: Kernel Timer Service (通用内核定时器)

## Why

项目存在 **3 处分散的定时器实现**,均不可复用且效率低下:

1. **`pdk/temporal_agent::WorkflowCallbackChannel::poll_loop`** — 200ms busy-poll (`std::this_thread::sleep_for(kPollInterval)`),无法扩展多订阅者
2. **`skill_interpreter::Impl::ipc_loop_and_wait`** — 100ms poll + EINTR 重试,精度不足
3. **`chat_session::Impl::input_thread_main`** — `std::getline(std::cin)` 无超时机制

3 处共同痛点:
- **精度差**:固定 100-200ms 粒度,长延迟唤醒漂移
- **CPU 浪费**:无 timer 触发也周期性唤醒
- **不可复用**:每个组件自己实现 timer wheel
- **未来扩展**:ADR-0066 V2 `host_register_timer` host function 需要底层定时器

**排期**: Sprint 28(2026-09-24 起)。**Sprint 27 容量已被 ADR-0087 root cause step 4 占用**(per `openspec/changes/adr-0087-root-cause-upgrade/proposal.md` §升级触发 第 4 步"验证 + 移除默认 SerializingDecorator",P0 阻塞 5 个 active changes)。**Oracle session `ses_f741f5d05ffeItVmfYVEjr67m3` 评审确认**:TimerService 是 microkernel 蓝图唯一短期可执行项,但 Sprint 27 应优先 P0 业务解锁,Sprint 28 再做架构沉淀。

## What Changes

- **新增** `include/agenticdsl/common/timer_service.h` — `ITimerService` 抽象契约(contract 层,PDK 可注入)
- **新增** `src/common/timer_service.cpp` — `TimerService` 实现(`std::jthread` + `std::condition_variable::wait_until` + 单调时钟)
- **新增** `tests/test_timer_service.cpp` — 单元测试(oneshot/periodic/cancel/漂移/线程安全)
- **改造** `pdk/temporal_agent/src/workflow_callback_channel.cpp` — `WorkflowCallbackChannel` 注入 `ITimerService*`(默认 std::jthread 实现),消除 `poll_thread_` busy-poll
- **不**改造 SkillInterpreter / ChatSession(V2 后续,本期聚焦 temporal_agent 试点)
- **不**改造其他 `std::this_thread::sleep_for` 散落代码(避免 scope 蔓延)

## Capabilities

### New Capabilities

- `kernel-timer-service`: 通用内核定时器抽象(`ITimerService` 契约 + monotonic-clock 实现 + 32 并发 timer 支持 + temporal_agent 迁移示例)

### Modified Capabilities

- `temporal-agent`: `WorkflowCallbackChannel::poll_thread_` 200ms busy-poll 替换为 TimerService 订阅;保持现有 5 个 tool + long-poll 语义不变

## Impact

- **依赖**: 无新外部依赖(`std::jthread` + `std::condition_variable` + `std::chrono::steady_clock` 均为 C++20 标准)
- **受影响代码 (3 文件)**:
  - `include/agenticdsl/common/timer_service.h` (新增, ~80 LOC)
  - `src/common/timer_service.cpp` (新增, ~150 LOC)
  - `pdk/temporal_agent/src/workflow_callback_channel.cpp` (改造, ~30 LOC 修改)
  - `pdk/temporal_agent/CMakeLists.txt` (新增 `agenticdsl_common` 依赖)
  - `tests/test_timer_service.cpp` (新增, ~120 LOC)
- **测试**: `test_timer_service` (~10 cases, ~30 assertions) + temporal_agent 既有 3 tests (回归)
- **CMake**: 根 `CMakeLists.txt` 添加 `agenticdsl_common` 子库 `timer_service.cpp`
- **Non-goals**:
  - ❌ cgroup/namespace(被 ADR-0055 seccomp+rlimit 覆盖,Oracle Q3 结论)
  - ❌ supervision tree(无多 child 场景,Oracle Q3)
  - ❌ procfs/sysfs introspection(无消费者,Oracle Q3)
  - ❌ PipeBus / UnixSocketBus(被 ADR-0059 ✅ Approved + ADR-0077 descoped 覆盖,Oracle Q2-d/e)
  - ❌ UserAgentLoader(等 ADR-0082 V2 subprocess 形态排期,Oracle Q2-a)
  - ❌ timerfd/epoll(Linux-only;cv 跨平台且精度满足 PDK 需求)
  - ❌ SkillInterpreter/ChatSession timer 改造(V2 follow-up,scope 控制)
  - ❌ 新增 ADR(本 change 是 contract 层工具,无架构决策点;Oracle Q1 建议)