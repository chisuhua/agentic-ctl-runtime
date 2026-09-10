# Design: Kernel Timer Service (通用内核定时器)

## Context

- 项目当前定时器实现散落 3 处,均基于 `std::this_thread::sleep_for` + `std::thread::join`:
  - `pdk/temporal_agent/src/workflow_callback_channel.cpp:46-74` — 200ms busy-poll (Sprint 22 ship)
  - `src/modules/skill_interpreter/skill_interpreter.cpp:355-535` — 100ms `poll()` + EINTR 重试 (ADR-0055 V1 ship)
  - `examples/pdk_chat_demo/chat_session.cpp:716-751` — `std::getline(std::cin)` 无超时
- 共同痛点:**精度差**(固定 sleep 粒度)、**CPU 浪费**(无事件也唤醒)、**不可复用**(每处自己实现)
- Oracle 评审(`ses_f741f5d05ffeItVmfYVEjr67m3`)确认:T1 方案(PipeBus/内核蓝图 ADR-0087/架构 v1.3)均因编号冲突/治理违规/规模错配被否决,**仅 TimerService 是 microkernel 蓝图可立即 ship 的部分**(Oracle Q1 结论)
- 项目状态:Sprint 25 ship (228→229 ctest PASS);Sprint 26 ship `upgrade-httplib-0541` + `adr-0087-root-cause-upgrade` proposal;**Sprint 27 容量被 ADR-0087 root cause step 4 占用**(移除默认 SerializingDecorator,P0 阻塞 5 个 active changes);**Sprint 28 可承接**本期 ~250 LOC 重构
- 约束: C++20 / CMake 3.20+ / 2 空格缩进 / 中文注释 / 单人开发模式 / 无新外部依赖

## Goals / Non-Goals

**Goals:**
- 抽象 `ITimerService` 契约(contract 层,PDK 可注入)
- cv + steady_clock 实现,跨平台(Linux/macOS/Windows)
- temporal_agent `WorkflowCallbackChannel` 迁移,消除 200ms busy-poll
- 单元测试覆盖 oneshot/periodic/cancel/漂移/线程安全
- 全量 ctest 零回归(229 baseline)
- 符合 AGENTS.md §Sprint 收官验证规则

**Non-Goals** (Oracle Q3 显式列出):
- ❌ **不**新建 microkernel 蓝图 ADR(等 UserAgentLoader ship 后再沉淀,Oracle Q3.1)
- ❌ **不**做 cgroup/namespace 隔离(seccomp+rlimit 已够,Oracle Q3.4)
- ❌ **不**做 supervision tree(无多 child 场景,Oracle Q3.4)
- ❌ **不**做 procfs/sysfs introspection(无消费者,Oracle Q3.4)
- ❌ **不**做 PipeBus(被 ADR-0059 ✅ Approved + ADR-0077 descoped 覆盖,Oracle Q2-d/e)
- ❌ **不**做 UserAgentLoader(等 ADR-0082 V2 subprocess 形态排期,Oracle Q2-a)
- ❌ **不**改造 SkillInterpreter IPC timeout(Sprint 28+ follow-up)
- ❌ **不**改造 ChatSession input_thread(V2 follow-up)
- ❌ **不**用 timerfd/epoll(Linux-only,跨平台 cv 满足需求)

## Decisions

### D1: TimerService 是 contract/common 层(非 kernel service)

**决策**: `ITimerService` 抽象放 `include/agenticdsl/common/timer_service.h`(contract/common 层),同 `EventBuilder` 先例(ADR-0068)。

**理由**:
- 避免 PDK 反向依赖 kernel service 风险(Oracle Q2-c 警告:temporal_agent 是 PDK user space 插件,若 TimerService 是 kernel service 则 PDK 反向依赖 kernel,违反 ADR-0021 P3 "PDK 头文件仅依赖 agenticdsl/contract/*.h")
- 与 `EventBuilder` / `CancellationRegistry` 同一层级,纳入 `agenticdsl_common` 静态库
- 未来若需 kernel service,可将 contract 抽象升格(0 改 PDK 代码)

**Alternatives**: 
- 放 `include/agenticdsl/kernel/`(L0) → 拒绝:PDK 依赖违规
- 放 `include/agenticdsl/core/` → 拒绝:语义混淆,core 是 DSLEngine 内部

### D2: 实现 = cv + wait_until(非 timerfd)

**决策**: TimerService 实现 = `std::jthread` + `std::condition_variable::wait_until(deadline)` + `std::chrono::steady_clock`。

**理由**:
- 跨平台(Linux/macOS/Windows 全部支持 C++20 std 库)
- ~150 LOC,简单可读
- 精度满足 PDK 需求(temporal_agent 200ms → cv 唤醒误差 < 10ms)
- 无 epoll/timerfd 系统调用,与 seccomp 25 syscall 白名单兼容

**Alternatives**:
- `timerfd_create` + epoll → 拒绝:Linux-only,scope 收缩
- `std::priority_queue` 外部调度 + 单独 worker → 拒绝:增加复杂度,无显著收益(PDK timer 数 < 32)
- `boost::asio` deadline_timer → 拒绝:新增外部依赖,违反"零新增依赖"

### D3: TimerId = uint64_t,单调递增

**决策**: `TimerId` 为 `uint64_t`,每次 `register_oneshot/periodic` 时 `next_id_.fetch_add(1)`。

**理由**:
- 单进程 namespace,无需跨进程
- 0 / 1 / 2... 单调递增,便于测试断言
- 取消 `cancel(id)` 返回 bool,O(1) hashmap lookup

**Alternatives**:
- `std::pair<thread_id, counter>` → 拒绝:增加复杂度,无实际需求
- UUID → 拒绝:128-bit 浪费,且不跨进程

### D4: periodic 漂移处理 = 累积式 deadline

**决策**: periodic timer 下次触发时间 = **prev_deadline + period**(非 now + period)。

**理由**:
- 避免累积漂移(若 handler 执行 50ms,now+period 模式 200ms timer 实际 250ms 周期)
- 与 Linux `timerfd_settime` 累积语义一致
- 测试:启动 100ms periodic,触发 10 次后,实际耗时应 ~1000ms(±50ms),非 1500ms

**Alternatives**:
- now + period → 接受但有漂移
- catch-up (一次补发多次) → 拒绝:可能 burst,违反 fire-once semantics

### D5: 注册线程不安全(单线程注册契约)

**决策**: `register_oneshot/periodic/cancel` 不要求线程安全,需在 start/stop 启动阶段单线程调用。

**理由**:
- 与 `DomainWorkerPool::register_domain_handler` 一致(单线程注册契约)
- 简化实现:无需 rwlock 保护 handler map
- 运行时(worker thread)只读取 next_deadline + cv wait,无竞争

**Alternatives**:
- thread-safe register → 接受但需 shared_mutex,增加 30 LOC
- 全部锁 → 拒绝:过设计,无实际并发场景

### D6: temporal_agent 注入 ITimerService*(默认 std::jthread 实现)

**决策**: `WorkflowCallbackChannel` 构造函数新增 `ITimerService* timer = nullptr` 参数;null 则创建内嵌 `TimerService` 实例(default)。

**理由**:
- 保持 PDK 插件可用性(无 main 改造)
- 显式 DI 利于测试(mock ITimerService 注入)
- 与 `SkillCapability` 默认值模式一致(ADR-0055 §决策 3)

**Alternatives**:
- 强制外部注入 → 拒绝:PDK 插件需可用,main 不强制改造
- 全局单例 → 拒绝:测试性差,违反 ADR-0021 §3.5

## Risks / Trade-offs

| 风险 | 缓解 |
|------|------|
| [timer 累积漂移] → 200ms periodic 实际 250ms | D4 累积 deadline 语义 + 单测断言 |
| [取消线程竞争] → cancel 与 worker 唤醒次序 | cancel 通过 cv.notify 串行化;cancel 返回 bool 表示 worker 是否仍持有 |
| [handler 抛异常] → worker 死亡 | worker_loop 内 try-catch + catch(...) 同 DomainWorkerPool §处理 |
| [jthread 析构 hang] → 取消回调未完成 | std::jthread 析构自动 request_stop + join (C++20 RAII),worker 唤醒后 cv.wait_until 立即返回 |
| [steady_clock 后退] → system clock 跳变影响 | steady_clock 不受系统时间影响,NTP 跳变无影响 |
| [TimerService 单例线程争用] → 多 PDK 共享 | 单一 TimerService 实例支持多订阅者(map<id, Entry>),worker 顺序 dispatch |

## Migration Plan

1. **M1 前置**: 确认 `std::jthread` + `condition_variable::wait_until` 在 C++20 编译通过(gcc 12+ / clang 15+)
2. **创建文件**: `include/agenticdsl/common/timer_service.h` + `src/common/timer_service.cpp`
3. **CMake 注册**: `src/common/CMakeLists.txt` 添加 `timer_service.cpp` 子库
4. **TDD 实施**: 5 步 TDD 模式(per AGENTS.md §ENGINEERING PATTERNS):写失败测试 → 验证 fail → 实现 → 验证 pass → commit
5. **temporal_agent 迁移**: `WorkflowCallbackChannel` 构造函数注入 + 消除 `poll_thread_`,改用 `timer->register_periodic(200ms, ...)`
6. **回归**: 全量 ctest 229 baseline + 新 `test_timer_service` 10 cases PASS
7. **SHIP-with-fixes 流程**: 派 Oracle 复核(新开 session)→ 拿 APPROVE → openspec archive + active-status.md 同步

## Open Questions

- [ ] 启动阶段 (Task 1.1): `std::jthread` 在 macOS/Windows 编译器版本要求? (gcc 12+ / clang 15+ 已知 OK,vs2019 16.10+ OK)
- [ ] Temporal_agent 迁移 (Task 4.x): `WorkflowCallbackChannel` 是否需保持 200ms 默认值,还是可配置?(默认 200ms 与 ADR-0055 polling 一致,保持)
- [ ] 测试并发度 (Task 3.x): 注册 32+ 个 timer 同时触发,是否需要并发安全?(D5 单线程注册,无需;handler 执行并发安全即可)

## 架构合规性检查

- **本 change 遵循**: ADR-0067 (L2/L3/L4 分层) — TimerService 放 contract/common 层(L2 边界),PDK 可注入
- **本 change 遵循**: ADR-0021 (PDK Design) §3.5 — PDK 插件可注入 contract 层接口,temporal_agent DI 不破坏独立性
- **本 change 不修改任何现有 ADR**: 0 amendment, 0 v1.x bump
- **本 change 不修改 docs/specs/architecture.md**: TimerService 是 L2 contract 实现,不改变 L0-L4 分层(Oracle Q2-g:kernel/user 与 L0-L4 是正交视图)
- **本 change 不新建 ADR**: 是 contract 层工具实现,无架构决策点(Oracle Q1 建议:不新建 microkernel 蓝图 ADR,等沉淀)
- **本 change 与 active changes 关系**: 与 `adr-0087-root-cause-upgrade` + `upgrade-httplib-0541` (Sprint 26) **不冲突**(范围不同,本 change 排 Sprint 28,因 Sprint 27 容量被 ADR-0087 root cause step 4 占用)
- **本 change 与 descoped ADR 关系**: 与 ADR-0077 (gRPC data plane) + ADR-0076 (MCP server) **无冲突**(数据面/控制面属另一专题,本 change 仅在 L2 基础设施层)