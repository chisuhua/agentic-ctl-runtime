# ADR-0037 Implementation Scope Audit

> **生成时间**: 2026-07-31 (文档治理修正 — ADR-0037 状态 🔍 Proposed → 🟡 Partial 对齐后补录)
> **基础**: `tools/docs_drift_audit.py` 报告 (Scenario 4)
> **关联 ADR**: [adr-0037-causal-ordering.md](adr-0037-causal-ordering.md)
> **状态**: 🟡 Partial (audit 后保持)

## 状态

**📋 Audit** (impl-scope-audit 文档, 与 docs-code-drift-audit 配套使用)

🟡 Partial (audit 后保持 — CausalClock 已 ship, 分布式向量时钟系列为 ADR 内明确的 defer 项, 主 ADR 状态准确)

## Drift 摘要

`docs_drift_audit.py` 报告: ADR 声称 🟡 Partial, 但 4/6 个描述的类未在 src/include 中找到。
审计结论: **非 drift** — 4 个缺失类全部属于 ADR 正文声明的分布式向量时钟 defer 范围, 🟡 Partial 状态与实际实施精确匹配。

## 原始描述 vs 实际实施

| ADR 描述的类 | 实际状态 | 实际位置 (如有) | 备注 |
|--------------|---------|----------------|------|
| `CausalClock` | ✅ Shipped | `include/agenticdsl/contract/causal_clock.h` + `src/common/contract/causal_clock.h` | 2026-07-27 ship, 含 emit auto-tick (gap-analysis 2026-07-30 基线确认) |
| `causal_order.h` (判定函数, T6) | ✅ Shipped | `include/agenticdsl/contract/causal_order.h` | 2026-09-10 ship (OpenSpec change `2026-09-10-adr-0037-causal-ordering-completion` Commit 1) — header-only 三规则判定 (L2 → L1 → Concurrent) |
| `ToolResult::parent_trace` 字段 (T2 余量) | ✅ Shipped | `src/core/types/tool_result.h/.cpp` | 2026-09-10 ship (Commit 1) — `std::optional<std::string>` + JSON 序列化 |
| `EventBuilder::parent_trace()` setter (T3.0) | ✅ Shipped | `include/agenticdsl/contract/event_builder.h` | 2026-09-10 ship (Commit 2) — 仿 `.trace_id()` 模式 |
| `CognitiveWorker::submit_task(parent_trace)` (T4) | ✅ Shipped | `include/agenticdsl/cognitive/cognitive_worker.h` + `src/modules/cognitive/cognitive_worker.cpp` | 2026-09-10 ship (Commit 3) — 默认参数 + emit 透传 |
| `DomainWorkerPool::DomainTask.parent_trace` (T5) | ✅ Shipped | `include/agenticdsl/cognitive/domain_worker_pool.h` + `src/modules/cognitive/domain_worker_pool.cpp` | 2026-09-10 ship (Commit 4) — struct 字段 + emit 透传 |
| 跨 Worker 因果链集成测试 (T7+T8) | ✅ Shipped | `tests/test_causal_ordering.cpp` | 2026-09-10 ship (Commit 5) — Section A/B/C/D = 9 cases / 31 assertions |
| `VectorClock` | ⏸ Deferred | — | 分布式向量时钟, ADR 正文明确 defer (单机 InMemoryBus 阶段不需要) |
| `LamportClock` | ⏸ Deferred | — | 同上, 被 CausalClock 设计吸收, 不单独实施 |
| `EventSequencer` | ⏸ Deferred | — | 分布式事件定序, 随 VectorClock 一并 defer |
| `EventReorderBuffer` | ⏸ Deferred | — | 乱序事件重排缓冲, 跨进程场景才需要, defer |

## 结论

- **主 ADR 状态 🟡 Partial 准确**, 无需调整。
- 状态提升轨迹: 2026-06-26 🔍 Proposed 起草 → 2026-07-27 🟡 Partial (CausalClock + emit auto-tick ship) → 2026-09-10 🟡 Partial (T2 余量 + T3.0 + T4 + T5 + T6 + T7 + T8 全 ship, 见 `openspec/changes/archive/2026-09-10-adr-0037-causal-ordering-completion/`)。
- **转 ✅ Approved 条件**: 跨进程/分布式 EventBus 落地 (ADR-0046/0059 系列) 且 VectorClock 或等价定序机制实施。
