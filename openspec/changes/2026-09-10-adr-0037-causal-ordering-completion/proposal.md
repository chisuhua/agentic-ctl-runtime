# Proposal: 完成 ADR-0037 因果排序剩余任务 (T2 剩余 + T4 + T5 + T6 + T7 + T8)

## Why

ADR-0037 (`docs/adr/adr-0037-causal-ordering.md`) 状态 🟡 Partial — Change C `adr-0037-causal-clock` (2026-07-27 ship) 已实现 **L1 全局序列号**（`CausalClock::tick()` + `InMemoryBus::emit()` 自动填充 `BusEvent.causal_time`），但 ADR §实施计划 中 **T2 剩余 + T4 + T5 + T6 + T7 + T8 全部未 ship**：

| Task | 描述 | 状态 |
|------|------|:----:|
| T1 EventSequencer | 序列号生成器 | ⚠️ adapted（实际 ship `CausalClock` 成员，非 DI） |
| T2 ToolResult 3 字段 | sequence_number + parent_trace + timestamp_ns | 🟡 partial（仅 trace_id 来自 ADR-0023） |
| T3 InMemoryBus emit 集成 | 自动注入 | ✅ shipped（`inmemory_bus.cpp:62`） |
| T4 CognitiveWorker parent_trace | submit_task(parent_trace) | ❌ 未 ship |
| T5 DomainWorkerPool parent_trace | submit_task(parent_trace) | ❌ 未 ship |
| T6 causal_order.h 判定函数 | 3-rule happens-before | ❌ 未 ship |
| T7 单元测试覆盖 | 10+ cases | ❌ 未 ship |
| T8 集成测试 (跨 Worker) | A→B→C 因果链 + TSan | ❌ 未 ship |

**Oracle 审查 (2026-09-10, session ses_f75d61d38ffe1HkqV1vouJOHot) 实证 gap**：
- `include/agenticdsl/contract/causal_order.h` 在仓库中**不存在**（ADR §4.1 描述的三规则判定函数）
- `parent_trace` 在 `src/` + `include/` 全仓库**零出现**
- `tests/test_causal_ordering.cpp` (T8) 不存在
- 现有 `test_causal_clock.cpp` (5 cases) 仅覆盖 `CausalClock` 本身，不覆盖 L2/L3 + 跨 Worker 因果传播

**缺失的因果保障**：当前消费者无法用 `causal_time` 做跨事件 happens-before 判定（判定函数不存在）；CognitiveWorker / DomainWorkerPool 之间无法显式声明因果依赖（`parent_trace` 不存在）。这使得 ADR-0037 §验证标准 6 条中**"因果链正确性" + "happens-before 判定" + "并发检测" 3 条不达标**。

## What Changes

### 1. 新建 `causal_order.h` (T6) — header-only 判定函数

`#include "agenticdsl/contract/bus_event.h"` + `#include <optional>` + 公开 API:

```cpp
namespace agenticdsl::event {

enum class CausalRelation { ABeforeB, BBeforeA, Concurrent };

CausalRelation causal_order(const BusEvent& a, const BusEvent& b);

// 传递性由调用方链式推导 (ADR-0037 line 538 注明 — 函数本身仅判 1-hop)
bool happens_before(const BusEvent& a, const BusEvent& b);

} // namespace
```

**判定规则** (per ADR §4.1)：
1. **L2 优先**: 若 `a.payload.trace_id == b.payload.parent_trace` → `ABeforeB`；反向同理（注意：`trace_id`/`parent_trace` 是 ToolResult 字段，在 `BusEvent.payload` 内）
2. **L1 回退**: 若两者 `causal_time` 已知 → `causal_time` 小者先
3. **L3 默认**: 否则 → `Concurrent`

### 2. ToolResult 加 `parent_trace` 字段 (T2 剩余)

```cpp
// src/core/types/tool_result.h (追加, optional + JSON 序列化)
std::optional<std::string> parent_trace;  // ADR-0037 L2 因果链
```

保持与既有 `trace_id` 完全一致的承载模式（`tool_result.h:88` 已有 `std::optional<std::string> trace_id`），序列化字段名 `parent_trace`。

### 3. CognitiveWorker::submit_task 加 `parent_trace` 参数 (T4)

```cpp
// include/agenticdsl/cognitive/cognitive_worker.h
void submit_task(
    const std::string& task_id,
    const std::string& prompt,
    std::optional<std::string> parent_trace = std::nullopt  // ADR-0037 L2
);
```

**默认参数保证零调用方迁移**（项目已有此惯例 — 见 `cancellation-chain-step4-loop-apis` 的 `std::stop_token` 默认参数模式）。

### 4. DomainWorkerPool::submit_task 加 `parent_trace` 参数 (T5)

```cpp
// include/agenticdsl/cognitive/domain_worker_pool.h
struct DomainTask {
    std::string domain;
    std::string tool_name;
    nlohmann::json arguments;
    std::optional<std::string> output_key;
    std::optional<std::string> parent_trace;  // ADR-0037 L2 (新增)
};

void submit_task(DomainTask task);  // 已是 struct 入参, 仅加字段
```

### 5. 新建 `tests/test_causal_ordering.cpp` (T7 + T8)

| Section | Cases | 覆盖 |
|---------|-------|------|
| **causal_order() 纯函数** | 4 cases | L2 匹配 / L1 回退 / Concurrent 默认 / 同 trace 无 parent_trace |
| **传递性 (调用方链式)** | 1 case | A→B→C 链式推导 |
| **ToolResult 序列化** | 2 cases | parent_trace 字段 round-trip |
| **跨 Worker 因果链 (T8)** | 3 cases | CognitiveWorker A→B → bus 捕获 → causal_order 判 -1；DomainWorkerPool 同样；TSan 干净 |

## Capabilities

### New Capabilities
- `causal-ordering-completion`: ship ADR-0037 L2 因果链 + T6 判定函数 + T7/T8 测试集；为分布式向量时钟升级保留 API 兼容

### Modified Capabilities
- (无 — 修改既有 `causal-clock` capability：扩展 L2 字段 + 判定函数；保持 L1 `causal_clock.h` 不变)

## Impact

**代码影响**:
- 新增: `include/agenticdsl/contract/causal_order.h` (header-only, ~40 行)
- 新增: `tests/test_causal_ordering.cpp` (~200 行, 10 cases)
- 修改: `src/core/types/tool_result.h` — 加 1 字段 (1 行 + 序列化 2 处)
- 修改: `include/agenticdsl/cognitive/cognitive_worker.h/.cpp` — submit_task 加默认参数 (2 处签名 + 1 处存到 task_contexts_)
- 修改: `include/agenticdsl/cognitive/domain_worker_pool.h/.cpp` — DomainTask 加字段 + emit 时透传 (3 处)
- 修改: `tests/test_cognitive_worker.cpp` + `tests/test_domain_worker_pool.cpp` — 加 1-2 个 parent_trace 测试 case

**测试影响**:
- 新增: `test_causal_ordering` (10 cases)
- 修改: `test_cognitive_worker` + `test_domain_worker_pool` 各 +1-2 cases
- 全量 ctest 零回归 (baseline 228/228)

**Single-dev 流程**:
- 按 `docs/adr/adr-self-review-checklist.md` 12 项清单自审
- **不强制** 24h cooling-off（5 atomic commits 独立可验证 + Oracle 复核 = 等价 cooling-off 机制；single-dev 模式下作者自审 + Oracle 续 session APPROVE 已 substitute cooling-off 目的）
- Ship 后 ADR-0037 🟡 Partial 状态保持（分布式向量时钟仍 defer — 转 ✅ 需 EventBus 落地）

## Non-goals

- **不**实现 Lamport 时间戳 / 向量时钟 / EventSequencer DI（CausalClock 成员方案已 ship 且功能等价）
- **不**实现 ReorderBuffer (ADR §4.2 乱序重排，跨进程才需要)
- **不**修改 `causal_clock.h`（L1 已 ship，保持向后兼容）
- **不**修改 `BusEvent` 结构（`causal_time` 字段已在 Change A 预留并 ship）
- **不**翻转 ADR-0037 状态（分布式向量时钟仍 defer，按 🟡 Partial 保持）

## 升级触发

不适用。本 change 完成 ADR-0037 剩余 ship，**不**升级状态。升级到 ✅ Approved 的条件（per adr-0037-causal-ordering-impl-scope.md "结论"）：跨进程/分布式 EventBus 落地（ADR-0046/0059 系列）+ VectorClock 或等价定序机制实施。

## 估时

**~18h** (per ADR T4 3h + T5 2h + T6 3h + T7 5h + T8 5h)。独立估算一致。

**实施顺序** (single session 内可完成):
1. **T6** causal_order.h + T7 部分纯函数测试 (~6h) → ship 中间 commit
2. **T2 余量** ToolResult parent_trace (~1h)
3. **T4 + T5** Worker parent_trace 传播 (~5h) → ship 中间 commit
4. **T8** 跨 Worker 集成测试 (~5h)
5. **ADR-0037 状态更新** + ship gate (~1h)

## 关联文档

- `docs/adr/adr-0037-causal-ordering.md` (parent ADR, 🔍 Proposed → 🟡 Partial 已 ship L1)
- `docs/adr/adr-0037-causal-ordering-impl-scope.md` (impl-scope audit)
- `openspec/changes/archive/adr-0037-causal-clock/` (Phase 1 ship, Change C — CausalClock + emit auto-tick)
- `include/agenticdsl/contract/causal_clock.h` (L1 已 ship, 保持不变)
- `tests/test_causal_clock.cpp` (L1 既有测试, 5 cases)

## Open Questions

- (无 — Oracle 审查已给出明确实施优先级 + watch-out 列表)

## Watch-out (来自 Oracle 审查)

- `causal_order()` 只判 1-hop；测试不要断言函数内部传递闭包，传递性由调用方链式推导（ADR-0037 line 538）
- 跨 Worker 集成测试用 `wait_for_drain()` 同步，**不要** sleep
- `submit_task(parent_trace)` 必须用默认参数 + `std::optional` 保持调用方零迁移
- `parent_trace` 进 ToolResult 后保持与 `trace_id` 相同的 meta 承载方式（不进 `meta`，而是顶层字段 — 仿 trace_id）
- TSan preset 必须跑（跨 Worker 测试易出 race）
- 单 session 内可完成，无需 24h cooling-off 拆分（5 atomic commits 独立可验证 + Oracle 复核 = 等价 cooling-off 机制；与 Impact §Single-dev 流程 段一致）