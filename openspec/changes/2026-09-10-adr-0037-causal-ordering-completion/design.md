# Design: 2026-09-10-adr-0037-causal-ordering-completion

## Context

ADR-0037 (🔍 Proposed → 🟡 Partial 2026-07-27) 三层因果机制:

| Layer | 机制 | 当前状态 |
|-------|------|:--------:|
| L1 全局序列号 | `CausalClock::tick()` atomic uint64_t | ✅ ship (2026-07-27, Change C) |
| L2 因果链 | `parent_trace` 显式声明依赖 | ❌ **本次 ship** |
| L3 单调时间戳 | `steady_clock::time_point` | 🟡 part (causal_time 字段已 ship 在 BusEvent) |

**Oracle 审查 (2026-09-10)** 证实 gap:
- `causal_order.h` 实现不存在
- `parent_trace` 全仓库零出现
- 现有 `test_causal_clock.cpp` 仅覆盖 CausalClock 本身，不覆盖跨事件判定与跨 Worker 传播

**Foundation 已就绪** (本次直接复用):
- `include/agenticdsl/contract/causal_clock.h` — L1 完整
- `include/agenticdsl/contract/inmemory_bus.h:78` — `CausalClock causal_clock_` 成员
- `src/common/contract/inmemory_bus.cpp:62` — emit 自动 tick + attach
- `BusEvent.causal_time` 字段已在 Change A 预留并填充

## Goals / Non-Goals

**Goals**:
- Ship T6 `causal_order.h` 判定函数 (1-hop L2 → L1 回退 → Concurrent)
- Ship T2 余量 `ToolResult::parent_trace` 字段 + JSON 序列化
- Ship T4 `CognitiveWorker::submit_task(parent_trace)` + Worker emit 时透传
- Ship T5 `DomainWorkerPool::DomainTask.parent_trace` + Worker emit 时透传
- Ship T7+T8 `tests/test_causal_ordering.cpp` 10 cases (纯函数 + 序列化 + 跨 Worker)
- 跨 Worker 集成测试通过 TSan preset
- 全量 ctest 零回归

**Non-Goals**:
- 不实现 Lamport / Vector Clock / EventSequencer DI
- 不实现 ReorderBuffer (跨进程才需要)
- 不修改 `causal_clock.h` (L1 已 ship, 向后兼容)
- 不修改 `BusEvent` 结构
- 不翻转 ADR-0037 状态 (🟡 Partial 保持)

## Decisions

### Decision 1: `causal_order.h` header-only 实现

**选择**: 纯头文件，无 .cpp 配套。函数体 30 行内。

**理由**:
- 纯函数，无状态，无锁 — header-only 无 ABI 风险
- 模板实例化常量化 (CausalRelation 枚举小)
- 测试可直接 #include 编译，避免链接复杂度
- 与 `causal_clock.h` 既有模式一致（header-only）

**Alternatives considered**:
- .cpp 配套: 增加构建复杂度但无实际收益 — 拒绝

### Decision 2: 判定规则优先级 — L2 优先 L1 回退 Concurrent 默认

**选择** (per ADR §4.1):
```cpp
CausalRelation causal_order(const BusEvent& a, const BusEvent& b) {
    // L2 优先: trace_id == parent_trace 匹配
    if (a.trace_id.has_value() && b.parent_trace.has_value() &&
        *a.trace_id == *b.parent_trace) return CausalRelation::ABeforeB;
    if (b.trace_id.has_value() && a.parent_trace.has_value() &&
        *b.trace_id == *a.parent_trace) return CausalRelation::BBeforeA;

    // L1 回退: causal_time 比较
    if (a.causal_time != 0 && b.causal_time != 0) {
        if (a.causal_time < b.causal_time) return CausalRelation::ABeforeB;
        if (b.causal_time < a.causal_time) return CausalRelation::BBeforeA;
    }

    // L3 默认: 并发
    return CausalRelation::Concurrent;
}
```

**理由**:
- L2 是显式声明的因果，最强语义
- L1 是事件总线时序，单调但不蕴含因果（两个不同 Worker 的事件可能并发）
- 0 causal_time 是 "未填充" sentinel (BusEvent 字段默认 0) — 必须排除，否则误判

**Alternatives considered**:
- 仅用 L1 causal_time: 丢失显式因果链语义 — 拒绝 (ADR §4.1 明示规则 2 优先)
- 用 std::optional<CausalRelation>: 实际不可能 Concurrent 之外有 undefined — 拒绝

### Decision 3: `submit_task(parent_trace)` 默认参数 + std::optional

**选择**:
```cpp
void submit_task(
    const std::string& task_id,
    const std::string& prompt,
    std::optional<std::string> parent_trace = std::nullopt);
```

**理由**:
- 默认 `std::nullopt` 保持所有现有调用方零修改 (编译器警告可能但非 error)
- 项目既有惯例（cancellation-chain-step4-loop-apis 的 `std::stop_token token = {}` 默认参数）
- `std::optional<string>` 而非 `string` 默认空串：空串是合法 trace_id 命名空间，无法区分"无依赖" vs "trace_id 为空"

**Alternatives considered**:
- 新建 `submit_task_with_parent()` 重载: API 表面积增大 — 拒绝
- 强制传 parent_trace: 破坏向后兼容 — 拒绝

### Decision 4: `DomainTask::parent_trace` 字段位置

**选择**: `DomainTask` 结构体内追加字段（不改 submit_task 签名 — 已是 struct 入参）。

**理由**:
- `DomainTask` 已是 struct 入参（per `domain_worker_pool.h:156`），加字段零 API 破坏
- 与 `CognitiveWorker::submit_task` 模式对齐（L2 同源）

**Alternatives considered**:
- 新建 submit_task_overload(parent_trace): 与 CognitiveWorker 模式不一致 — 拒绝

### Decision 5: `ToolResult::parent_trace` 序列化字段名

**选择**: 顶层 JSON 字段 `parent_trace`，与既有 `trace_id` 完全同模式。

**理由**:
- `trace_id` 已是顶层字段（`tool_result.h:88`），不进 `meta`
- 一致性优先 — 消费者端 `causal_order()` 读 `a.trace_id / b.parent_trace` 路径清晰
- 序列化字段名直接 = C++ 字段名（已有惯例）

**Alternatives considered**:
- 放进 `meta.parent_trace`: 与 trace_id 不一致 — 拒绝
- 重命名 `parent_trace_id`: 与 ADR 文本不一致 — 拒绝

### Decision 6: 跨 Worker 集成测试同步机制

**选择**: 用 `InMemoryBus::wait_for_drain()` (inmemory_bus.cpp:116 已 ship) 同步，不用 sleep。

**理由**:
- 内部 cv + in_flight_callbacks 计数，精确等待所有事件分发完成
- sleep 易 flake（CPU 调度 + 测试时长不可控）
- 项目既有惯例（test_event_bus_soak.cpp 用此机制）

**Alternatives considered**:
- sleep + std::chrono: 易 flake — 拒绝
- barrier 计数: 跨 Worker 同步复杂 — 拒绝

### Decision 7: 5 步实施顺序

**选择**:
1. T6 causal_order.h (header-only, 纯函数)
3. T7 部分测试（causal_order() 4 cases + 传递性 + 序列化 2 cases）
4. T2 余量 ToolResult parent_trace + 序列化测试
5. T4 CognitiveWorker::submit_task(parent_trace) + 既有 test_cognitive_worker +1 case
6. T5 DomainWorkerPool::DomainTask.parent_trace + 既有 test_domain_worker_pool +1 case
7. T8 跨 Worker 集成测试 (CognitiveWorker + DomainWorkerPool 各 1 case)
8. TSan preset + 全量 ctest 零回归

**理由**:
- 纯函数先行 (零依赖,易 ship + 早验证核心判定)
- ToolResult 字段次之 (被后续 3 处 worker 透传依赖)
- Worker 改造 + 集成测试最后 (依赖前两步)
- 1+3+1+5+5+1 = 16h, 留 2h 缓冲

**Alternatives considered**:
- 一次性全部 ship: 单 commit 大, 调试难 — 拒绝

## Risks / Trade-offs

[Risk] `causal_order()` L1 回退时若 `causal_time == 0` (BusEvent 默认 sentinel) 会误判 Concurrent
→ Mitigation: 函数体内显式检查 `causal_time != 0`；测试覆盖 sentinel case (3.4)

[Risk] `submit_task(parent_trace)` 默认参数在某些编译器下可能产生"未使用参数" warning
→ Mitigation: 函数体内显式使用 (存入 task_contexts_) — 警告消除

[Risk] 跨 Worker 集成测试 TSan 偶发 race (catch2 framework 已知交互, 见 AGENTS.md §ENGINEERING PATTERNS 模式 2)
→ Mitigation: 用 `wait_for_drain()` 同步而非 sleep；TSan preset 验证

[Risk] `parent_trace` 进 ToolResult 后,旧 JSONL 数据无此字段 → 反序列化 backward compat
→ Mitigation: `std::optional<string>` + `nlohmann::json::value()` 缺值安全（tool_result.h 既有 `trace_id` 同模式，验证过）

[Risk] `causal_order` 1-hop 限制被用户误用为传递闭包
→ Mitigation: 头文件 doxygen 注释明确说明；测试 case 3.2 显式注释"调用方链式推导"

## Migration Plan

**单 session 5 步实施** (per Decision 7):

### Step 1: T6 causal_order.h + T7 部分纯函数测试
- 新建 `include/agenticdsl/contract/causal_order.h` (~40 行)
- 新建 `tests/test_causal_ordering.cpp` (Section A: causal_order 4 cases + Section B: 传递性 1 case)
- `ctest -R test_causal_ordering` PASS

### Step 2: T2 余量 ToolResult parent_trace
- 修改 `include/agenticdsl/types/tool_result.h` (加 1 字段 + 序列化对)
- `tests/test_causal_ordering.cpp` Section C: 序列化 2 cases (round-trip + 缺值容错)
- `ctest -R test_tool_result` + `test_causal_ordering` PASS

### Step 3: T4 CognitiveWorker::submit_task(parent_trace)
- 修改 `include/agenticdsl/cognitive/cognitive_worker.h` + `.cpp` (签名 + task_contexts_ 存 parent_trace + emit 时 meta 透传)
- `tests/test_cognitive_worker.cpp` +1 case (parent_trace 存到 task_contexts_, 后续 emit payload 可见)
- `ctest -R test_cognitive_worker` PASS

### Step 4: T5 DomainWorkerPool::DomainTask.parent_trace
- 修改 `include/agenticdsl/cognitive/domain_worker_pool.h` + `.cpp` (DomainTask 加字段 + emit 时透传)
- `tests/test_domain_worker_pool.cpp` +1 case
- `ctest -R test_domain_worker_pool` PASS

### Step 5: T8 跨 Worker 集成测试 + ship gate
- `tests/test_causal_ordering.cpp` Section D: 跨 Worker 3 cases (Cognitive A→B 因果链 / Domain A→B / TSan soak 1 case)
- `cmake --preset tsan -DAGENTICDSL_BUILD_TESTS=ON && ctest` 全绿
- `cmake --preset asan && ctest` 全绿
- `ctest` 默认 preset 228+10 = 238 PASS

### Step 6: Ship + ADR 状态同步
- 1 atomic commit (per Step) — 5 commits total
- ADR-0037 impl-scope-audit.md 更新: T2 余量 / T4 / T5 / T6 / T7 / T8 ✅ shipped
- ADR-0037 状态 🟡 Partial 保持 (分布式向量时钟仍 defer)
- `openspec validate --strict` PASS
- archive OpenSpec change

## Open Questions

- (无 — Oracle 已给出明确实施优先级 + watch-out 列表 + 5 步顺序)

## 兼容性保证

- `causal_clock.h` L1 API 不变 — 现有 `test_causal_clock.cpp` 5 cases 零修改
- `BusEvent.causal_time` 字段语义不变 — 现有 28 tests/test_*.cpp 零修改
- `CognitiveWorker::submit_task` 默认参数 — 现有所有调用方零修改（编译器可能 warn 但非 error）
- `DomainWorkerPool::submit_task(DomainTask)` 入参结构 — 加字段不影响调用方（既可省略字段）
- `ToolResult::parent_trace` 可选字段 — JSONL 旧数据无此字段，反序列化容错（nlohmann::json::value() 缺值安全）

## 测试覆盖汇总

| Test File | Cases | 覆盖内容 |
|-----------|:----:|---------|
| `tests/test_causal_ordering.cpp` Section A | 4 | causal_order 纯函数 L2/L1/Concurrent/同 trace |
| `tests/test_causal_ordering.cpp` Section B | 1 | 传递性调用方链式推导 |
| `tests/test_causal_ordering.cpp` Section C | 2 | parent_trace 序列化 round-trip + 缺值容错 |
| `tests/test_causal_ordering.cpp` Section D | 3 | 跨 Worker 因果链 (Cognitive + Domain + TSan soak) |
| `tests/test_cognitive_worker.cpp` | +1 | submit_task(parent_trace) 透传 |
| `tests/test_domain_worker_pool.cpp` | +1 | DomainTask.parent_trace 透传 |
| **总计新增** | **12 cases** | 全量 ctest 228+12 = 240 PASS |