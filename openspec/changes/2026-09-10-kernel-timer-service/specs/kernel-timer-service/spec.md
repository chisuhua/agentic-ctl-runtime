# kernel-timer-service Specification

通用内核定时器抽象层（contract/common 层），提供高精度 timer 注册、取消、跨多订阅者共享 worker 的能力，并提供 temporal_agent 长轮询迁移示例。

## ADDED Requirements

### Requirement: ITimerService 抽象契约 SHALL 提供

`ITimerService` SHALL 是一个 contract 层抽象接口（位于 `include/agenticdsl/contract/timer_service.h`），提供以下 API:

- `using TimerId = uint64_t` — 单调递增 timer 标识符
- `virtual TimerId register_oneshot(std::chrono::milliseconds delay, std::function<void()> cb) = 0` — 注册一次性 timer,`delay` 后触发一次回调;返回 `TimerId` 用于后续 `cancel`
- `virtual TimerId register_periodic(std::chrono::milliseconds period, std::function<void()> cb) = 0` — 注册周期性 timer,首次触发后每 `period` 触发一次,直至 `cancel`
- `virtual bool cancel(TimerId id) = 0` — 取消 timer,返回 true 表示成功取消,返回 false 表示 timer 不存在或已触发
- `virtual ~ITimerService() = default`

`ITimerService` SHALL 不要求线程安全（注册/取消仅在 start/stop 阶段单线程调用）。

#### Scenario: 注册 oneshot timer

- GIVEN `TimerService` 实例
- WHEN 调用 `register_oneshot(100ms, [] { /* callback */ })`
- THEN 返回 `TimerId` 非 0
- AND 100ms 后 callback 被执行恰好一次

#### Scenario: 注册 periodic timer

- GIVEN `TimerService` 实例
- WHEN 调用 `register_periodic(50ms, [] { /* callback */ })`
- THEN 返回 `TimerId` 非 0
- AND 50ms 后 callback 第 1 次触发
- AND 之后每 50ms 触发一次,直至 `cancel`

#### Scenario: 取消未触发 timer

- GIVEN `TimerService` 实例 + 已注册 oneshot timer (delay=1000ms)
- WHEN 调用 `cancel(timer_id)`
- THEN 返回 `true`
- AND 1000ms 后 callback **不**被触发

#### Scenario: 取消已触发 timer

- GIVEN `TimerService` 实例 + 已触发 oneshot timer
- WHEN 调用 `cancel(timer_id)`
- THEN 返回 `false`(timer 不在 map 中)

#### Scenario: 取消不存在的 timer

- GIVEN `TimerService` 实例
- WHEN 调用 `cancel(99999)`(未注册的 id)
- THEN 返回 `false`

### Requirement: TimerService 实现 SHALL 基于 cv + steady_clock

`TimerService` SHALL 使用 `std::jthread` + `std::condition_variable::wait_until` + `std::chrono::steady_clock` 实现:

- 单 worker thread(`std::jthread`,RAII 自动 join)
- `std::map<TimerId, TimerEntry>` 存储活跃 timer,O(log N) 插入/删除
- 每次 `wait_until` 唤醒后扫描 map,触发所有到期 timer
- `periodic` 下次触发时间 = **prev_deadline + period**(累积式,避免漂移)
- handler 异常 SHALL NOT kill worker (try-catch + catch(...) 隔离)
- 析构函数 SHALL 通过 `std::jthread` 析构 RAII 自动 `request_stop` + `join`

#### Scenario: 跨平台编译

- GIVEN `TimerService` 实现
- WHEN 在 Linux (gcc 12+) / macOS (clang 15+) / Windows (msvc 19.30+) 编译
- THEN 编译通过(仅依赖 C++20 std 库)

#### Scenario: 精度验证 (漂移 < 10ms)

- GIVEN `TimerService` + `register_oneshot(100ms, cb)`
- WHEN cb 被触发
- THEN 实际耗时 ∈ [100ms, 110ms](steady_clock 精度 + cv 唤醒延迟)

#### Scenario: periodic 无累积漂移

- GIVEN `TimerService` + `register_periodic(100ms, cb)` + cb 执行 ~10ms
- WHEN cb 被触发 10 次
- THEN 总耗时 ∈ [1000ms, 1100ms](累积 deadline 语义,**非** 1500ms)

#### Scenario: handler 异常隔离

- GIVEN `TimerService` + `register_periodic(100ms, [] { throw std::runtime_error("oops"); })`
- WHEN handler 抛异常
- THEN worker thread **不**死亡,继续等待下一次触发

#### Scenario: 析构自动 join

- GIVEN `TimerService` 实例
- WHEN 实例离开作用域
- THEN `std::jthread` 析构自动 `request_stop` + `join`(RAII)
- AND 无 thread leak (`ps -T -p <pid>` 验证)

### Requirement: TimerService 单元测试 SHALL 覆盖核心场景

`tests/test_timer_service.cpp` SHALL 覆盖以下 case (Catch2):

1. `register_oneshot_fires_after_delay` — oneshot 在指定 delay 后触发
2. `register_oneshot_fires_exactly_once` — oneshot 只触发一次
3. `register_periodic_fires_multiple_times` — periodic 触发 ≥3 次
4. `cancel_prevents_callback` — 取消后 callback 不触发
5. `cancel_returns_false_for_unknown_id` — 取消不存在 id 返回 false
6. `cancel_returns_false_for_fired_oneshot` — 取消已触发 oneshot 返回 false
7. `periodic_no_accumulated_drift` — 10 次触发总耗时 < 1100ms
8. `handler_exception_does_not_kill_worker` — handler 抛异常后 worker 继续运行
9. `multiple_timers_independent` — 多个 timer 独立触发
10. `destructor_joins_worker_cleanly` — 析构无 thread leak

#### Scenario: 全测试 PASS

- GIVEN `TimerService` 完整实现 + `test_timer_service.cpp`
- WHEN `ctest -R test_timer_service --output-on-failure`
- THEN 10 cases 全部 PASS (≥30 assertions)

#### Scenario: 全量 ctest 零回归

- GIVEN 本 change ship
- WHEN `HYDRAFORGE_SKIP_REAL_LLM=1 ctest --test-dir build -j$(nproc)`
- THEN **230/230 PASS** (229 baseline + 新 test_timer_service 1 binary 10 cases)
- AND `python3 tools/adr_lint.py` 0 errors
- AND `python3 tools/docs_drift_audit.py` 0 DRIFT
- AND `openspec validate --strict` exit 0

### Requirement: temporal_agent WorkflowCallbackChannel SHALL 迁移至 TimerService

`pdk/temporal_agent::WorkflowCallbackChannel` SHALL 注入 `ITimerService*`(默认 nullptr,内部创建 `TimerService` 实例),消除 `std::thread poll_thread_` + 200ms `sleep_for` busy-poll。

迁移前(当前):
```cpp
void WorkflowCallbackChannel::poll_loop() {
    while (running_.load(...)) {
        auto signals = backend_->consume_signals(workflow_id_);
        for (const auto& sig : signals) { /* dispatch */ }
        std::this_thread::sleep_for(kPollInterval);  // 200ms busy-poll
    }
}
```

迁移后(目标):
```cpp
WorkflowCallbackChannel::WorkflowCallbackChannel(std::string workflow_id,
                                                  ITimerService* timer = nullptr)
    : workflow_id_(std::move(workflow_id)), timer_(timer ? timer : new TimerService()) {
    owns_timer_ = (timer == nullptr);
}

void WorkflowCallbackChannel::start_polling(std::shared_ptr<ITemporalBackend> backend) {
    backend_ = std::move(backend);
    running_.store(true);
    timer_->register_periodic(std::chrono::milliseconds(200),
                              [this] { poll_once(); });
}

void WorkflowCallbackChannel::poll_once() {
    if (!running_.load()) return;
    auto signals = backend_->consume_signals(workflow_id_);
    for (const auto& sig : signals) {
        // 派发 handler (锁外 + 异常隔离)
    }
}
```

#### Scenario: 现有 temporal_agent 测试零回归

- GIVEN `WorkflowCallbackChannel` 迁移至 TimerService
- WHEN 运行 `ctest -R temporal_agent --output-on-failure`
- THEN 既有 3 tests 全部 PASS (Sprint 22 ship)
- AND `test_temporal_agent_streaming` 零回归

#### Scenario: 200ms 周期语义保持

- GIVEN `WorkflowCallbackChannel::start_polling` 注入 TimerService
- WHEN 启动后观察 5 秒
- THEN backend consume_signals 被调用 ~25 次 (5s / 200ms)
- AND 调用间隔 ∈ [180ms, 220ms](±20ms cv 漂移容忍)

#### Scenario: 析构无 thread leak

- GIVEN `WorkflowCallbackChannel` 实例
- WHEN 实例离开作用域
- THEN TimerService 内嵌实例正确析构
- AND 无 thread leak

## MODIFIED Requirements

### Requirement: temporal-agent WorkflowCallbackChannel 行为 SHALL 保持

`temporal-agent` 既有 5 个 tool 行为 SHALL 不变:
- `temporal/start_workflow` — 阻塞启动 + 轮询直到完成
- `temporal/start_async` — 异步启动 + 立即返回
- `temporal/poll` — 轮询工作流状态
- `temporal/signal` — 发送信号
- `temporal/query` — 查询只读元数据

`WorkflowCallbackChannel` 5 个 tool handler SHALL 在调用方线程同步执行(`pdk_register_tools` 注册路径不变),TimerService 仅替换内部 poll loop。

#### Scenario: 5 个 tool 调用路径不变

- GIVEN `WorkflowCallbackChannel` 迁移后
- WHEN 调用 `temporal/start_workflow` tool (经 DSLEngine / TopoScheduler / NodeExecutor / ToolCoordinator / ToolRegistry)
- THEN tool 在调用方线程同步执行
- AND 返回 `WorkflowResult` JSON 序列化结果
- AND 行为与 Sprint 22 ship 一致

#### Scenario: 长轮询周期保持 200ms

- GIVEN `WorkflowCallbackChannel::start_polling`
- WHEN 后端无 signal
- THEN 持续 200ms 轮询(语义保持)
- AND 200ms 后端返回 signal → 立即派发
- AND 派发后继续 200ms 周期

## CROSS-REFERENCES

- **依赖**: ADR-0067 (L2/L3/L4 分层) — TimerService 放 contract/common 层
- **依赖**: ADR-0021 (PDK Design) §3.5 — PDK 注入 contract 层接口
- **正交**: ADR-0037 (CausalClock) — logical time 与 physical time 语义独立
- **正交**: ADR-0055 (skill-isolation) — SkillInterpreter IPC timeout 改造属后续 Sprint
- **不影响**: ADR-0076/0077 (MCP/gRPC,descoped) — 数据面/控制面属另一专题
- **不影响**: ADR-0082 (agent-first-class-registry) — UserAgentLoader 等 V2 subprocess 形态
- **不实施清单**: cgroup/namespace / supervision tree / procfs/sysfs / PipeBus(per Oracle Q3)
