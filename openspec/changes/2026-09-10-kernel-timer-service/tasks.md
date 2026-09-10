# Tasks: Kernel Timer Service (通用内核定时器)

## 1. 前置验证 (M1: 编译器 + 头文件)

- [ ] 1.1 验证 `std::jthread` + `condition_variable::wait_until` 在 gcc 12+ / clang 15+ / msvc 19.30+ 可用 (`grep -rn "std::jthread" external/ | head -5` 确认项目已有先例)
- [ ] 1.2 确认 `include/agenticdsl/common/` 目录存在且 CMake 注册 (`cat src/common/CMakeLists.txt` 验证新增 .cpp 路径)

## 2. 接口契约 (D1: contract 层定义)

- [ ] 2.1 创建 `include/agenticdsl/common/timer_service.h` (~80 LOC)
  - `class ITimerService` 抽象接口
  - `using TimerId = uint64_t`
  - 3 个纯虚函数:`register_oneshot` / `register_periodic` / `cancel`
  - 头文件头注释:功能描述、设计依据(ADR-0067 + ADR-0021)、作者、Sprint 28 日期(本 change 已顺延至 Sprint 28,因 Sprint 27 容量被 ADR-0087 root cause step 4 占用)
- [ ] 2.2 编译验证: 在 `src/common/CMakeLists.txt` 添加 placeholder,确认 `agenticdsl_common` 库 header 可见

## 3. 测试驱动 (TDD 5 步: 写失败测试 → 验证 fail → 实现 → 验证 pass → commit)

### 3.1 写失败测试

- [ ] 3.1.1 创建 `tests/test_timer_service.cpp` (~120 LOC, Catch2)
  - 10 cases (per spec §Requirement 单元测试):
    1. `register_oneshot_fires_after_delay`
    2. `register_oneshot_fires_exactly_once`
    3. `register_periodic_fires_multiple_times`
    4. `cancel_prevents_callback`
    5. `cancel_returns_false_for_unknown_id`
    6. `cancel_returns_false_for_fired_oneshot`
    7. `periodic_no_accumulated_drift`(关键:10 次触发总耗时 < 1100ms)
    8. `handler_exception_does_not_kill_worker`
    9. `multiple_timers_independent`
    10. `destructor_joins_worker_cleanly`
  - 头注释:接 ADR-0067 / spec.md §Requirement 单元测试场景
- [ ] 3.1.2 `cmake --build build --target test_timer_service` 编译 → **预期 FAIL**(TimerService 未实现)

### 3.2 验证失败

- [ ] 3.2.1 确认编译错误指向 `TimerService` 类未定义(非其他错误)
- [ ] 3.2.2 记录 baseline:全量 ctest 229 baseline + 编译 0 失败

### 3.3 实现 TimerService

- [ ] 3.3.1 创建 `src/common/timer_service.cpp` (~150 LOC)
  - `class TimerService : public ITimerService`
  - 成员:
    - `struct TimerEntry { std::chrono::steady_clock::time_point deadline; std::chrono::milliseconds period{0}; std::function<void()> cb; TimerId id; }`
    - `std::jthread worker_` (RAII)
    - `std::mutex mtx_` (保护 timer map)
    - `std::condition_variable cv_`
    - `std::atomic<uint64_t> next_id_{1}`
    - `std::atomic<bool> running_{true}`
    - `std::map<TimerId, TimerEntry> timers_`
  - `TimerService()` 启动 worker thread
  - `~TimerService()` jthread 析构自动 join
  - `TimerId register_oneshot(delay, cb)` 加锁插入 + cv.notify
  - `TimerId register_periodic(period, cb)` 同上
  - `bool cancel(id)` 加锁查找 + erase + cv.notify
  - `void worker_loop()` while(running_) cv.wait_until(next_deadline),扫描 map 触发到期 timer
  - **关键**:periodic 下次 deadline = `prev_deadline + period` (累积式, D4)
  - **关键**:handler 调用 try-catch + catch(...) (异常隔离, D2)
- [ ] 3.3.2 在 `src/common/CMakeLists.txt` 注册 `timer_service.cpp` 到 `agenticdsl_common` 静态库目标

### 3.4 验证通过

- [ ] 3.4.1 `cmake --build build -j$(nproc)` 编译成功
- [ ] 3.4.2 `ctest --test-dir build -R test_timer_service --output-on-failure` → **10/10 PASS (≥30 assertions)**
- [ ] 3.4.3 特别验证 case 7 (漂移):100ms periodic × 10 次,总耗时 [1000ms, 1100ms]
- [ ] 3.4.4 全量 ctest `HYDRAFORGE_SKIP_REAL_LLM=1 ctest --test-dir build -j$(nproc)` → **230/230 PASS**

### 3.5 Commit (原子 commit #1: 接口 + 实现 + 测试)

- [ ] 3.5.1 atomic commit: "feat(common): ITimerService 抽象 + TimerService cv 实现 + 10 case 单元测试"
- [ ] 3.5.2 commit 信息包含:Task 进度 + ctest baseline→230 + Oracle session ID (待 4.x 派发后填)

## 4. temporal_agent 迁移 (D6: 消除 poll_thread_)

- [ ] 4.1 修改 `pdk/temporal_agent/src/workflow_callback_channel.h`
  - 构造函数新增参数:`ITimerService* timer = nullptr`
  - 新增成员:`ITimerService* timer_{nullptr}` + `bool owns_timer_{false}`
  - 删除成员:`std::thread poll_thread_` + `std::atomic<bool> running_{false}`
  - 新增方法:`void poll_once()`
- [ ] 4.2 修改 `pdk/temporal_agent/src/workflow_callback_channel.cpp`
  - 构造函数:timer==nullptr 时 `timer_ = new TimerService()` + `owns_timer_ = true`
  - 析构函数:若 `owns_timer_` 则 `delete timer_`
  - `start_polling()`:删除 `poll_thread_ = std::thread(...)`,改为 `timer_->register_periodic(200ms, [this] { poll_once(); })`
  - `stop()`:删除 `poll_thread_.join()`,改为 `timer_->cancel(periodic_id)` + `running_ = false`
  - `poll_once()`:从原 poll_loop 提取信号消费 + handler 派发逻辑
- [ ] 4.3 修改 `pdk/temporal_agent/CMakeLists.txt`
  - 添加 `agenticdsl_common` 链接依赖(`target_link_libraries(TemporalAgent PRIVATE agenticdsl_common)`)
- [ ] 4.4 编译验证:`cmake --build build --target TemporalAgent` exit 0

## 5. 回归测试 (D6 验证 + 全量 ctest)

- [ ] 5.1 temporal_agent 专项:`ctest --test-dir build -R temporal_agent --output-on-failure` → 既有 3 tests + test_temporal_agent_streaming 全 PASS(零回归)
- [ ] 5.2 200ms 周期语义保持:在 test_temporal_agent_streaming 中添加断言(可选) — 后端 mock 接收 ≥3 次 `consume_signals` 调用且间隔 ∈ [180ms, 220ms]
- [ ] 5.3 析构无 thread leak:`ps -T -p <pid>` 在 WorkflowCallbackChannel 析构前后 thread 数不变
- [ ] 5.4 全量 ctest:`HYDRAFORGE_SKIP_REAL_LLM=1 ctest --test-dir build -j$(nproc)` → **230/230 PASS 零回归**

## 6. Commit + 文档同步

- [ ] 6.1 atomic commit #2: "refactor(pdk/temporal_agent): WorkflowCallbackChannel 迁移至 TimerService"
- [ ] 6.2 更新 `docs/active-status.md` Sprint 28 行:`kernel-timer-service` ship(本 change 已顺延至 Sprint 28,因 Sprint 27 容量被 ADR-0087 step 4 占用)
- [ ] 6.3 更新 `AGENTS.md` §ENGINEERING PATTERNS:沉淀 TimerService pattern(契约层工具 + cv 跨平台模式 + 漂移处理)

## 7. SHIP-with-fixes 流程 (per AGENTS.md §4)

- [ ] 7.1 派 Oracle 复核(新开 session `ses_<NEW>`)→ 拿 APPROVE
- [ ] 7.2 修复 Oracle SHIP-with-fixes 清单(按严重度排序,新增独立 commit 保持原子性)
- [ ] 7.3 `python3 tools/adr_lint.py` PASS + `python3 tools/docs_drift_audit.py` 0 DRIFT
- [ ] 7.4 `openspec validate --strict <change-name>` exit 0
- [ ] 7.5 `openspec archive 2026-09-10-kernel-timer-service` 归档
- [ ] 7.6 更新 `docs/README.md` 状态表(如需) + AGENTS.md §Recent Changes 追加 ship 记录

## 8. 不实施清单(显式 ship 后仍记录)

- ❌ cgroup/namespace(seccomp+rlimit 已够,Oracle Q3)
- ❌ supervision tree(无多 child 场景,Oracle Q3)
- ❌ procfs/sysfs introspection(无消费者,Oracle Q3)
- ❌ PipeBus / UnixSocketBus(被 ADR-0059 + ADR-0077 覆盖,Oracle Q2)
- ❌ UserAgentLoader(等 ADR-0082 V2,Oracle Q2)
- ❌ timerfd/epoll(Linux-only,跨平台 cv 满足,Oracle D2)
- ❌ SkillInterpreter/ChatSession timer 改造(V2 follow-up,scope 控制)
