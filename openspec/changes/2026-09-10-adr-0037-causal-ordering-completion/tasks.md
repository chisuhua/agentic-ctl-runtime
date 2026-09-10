## 1. T6 causal_order.h + T7 纯函数测试 (~6h)

### 1.1 头文件实现

- [ ] 1.1.1 新建 `include/agenticdsl/contract/causal_order.h`
  - `enum class CausalRelation { ABeforeB, BBeforeA, Concurrent }`
  - `CausalRelation causal_order(const BusEvent& a, const BusEvent& b)` — 3 规则判定
  - `bool happens_before(const BusEvent& a, const BusEvent& b)` — wrapper
  - doxygen 注释明确 1-hop 限制（ADR-0037 line 538）

### 1.2 测试文件骨架

- [ ] 1.2.1 新建 `tests/test_causal_ordering.cpp` — Catch2 `TEST_CASE` tag `[causal_ordering]`
  - Section A: causal_order 纯函数 4 cases (L2 匹配 / L1 回退 / Concurrent 默认 / 同 trace 无 parent)
  - Section B: 传递性 1 case (调用方链式推导)

### 1.3 验证

- [ ] 1.3.1 (verify) `ctest -R test_causal_ordering` 5/5 PASS
- [ ] 1.3.2 (verify) `ctest` 全量 228+5 = 233 PASS 零回归

## 2. T2 余量 ToolResult parent_trace (~1h)

### 2.1 字段添加

- [ ] 2.1.1 `src/core/types/tool_result.h` — 在 `trace_id` 旁加 `std::optional<std::string> parent_trace;`
- [ ] 2.1.2 `src/core/types/tool_result.h` — 序列化函数 `to_json()` 输出 `"parent_trace"` key (有值时)
- [ ] 2.1.3 `src/core/types/tool_result.h` — 反序列化 `from_json()` 缺值容错 (`j.value("parent_trace", std::optional<std::string>{})` 或 `contains` + 显式处理)

### 2.2 测试扩展

- [ ] 2.2.1 `tests/test_causal_ordering.cpp` Section C — 序列化 2 cases
  - 字段存在 round-trip
  - 缺值容错（无 `parent_trace` key 的 JSON → 反序列化为 std::nullopt）

### 2.3 验证

- [ ] 2.3.1 (verify) `ctest -R test_causal_ordering` 7/7 PASS (5+2)
- [ ] 2.3.2 (verify) `ctest -R test_tool_result` 全绿
- [ ] 2.3.3 (verify) 全量 ctest 228+7 = 235 PASS 零回归

## 3. T4 CognitiveWorker::submit_task(parent_trace) (~3h)

### 3.1 Worker 接口改造

- [ ] 3.1.1 `include/agenticdsl/cognitive/cognitive_worker.h` — `submit_task` 加第 3 参数 `std::optional<std::string> parent_trace = std::nullopt`
- [ ] 3.1.2 `src/modules/cognitive/cognitive_worker.cpp` — `submit_task` 实现体存入 `task_contexts_[task_id].parent_trace`
- [ ] 3.1.3 `src/modules/cognitive/cognitive_worker.cpp` — `worker_loop` 中发射 `started/completed` 事件时，将 `parent_trace` 写入 `payload.meta.parent_trace` 或顶层字段（保持与既有 trace_id 字段语义一致）

### 3.2 测试扩展

- [ ] 3.2.1 `tests/test_cognitive_worker.cpp` 新增 case 10: `submit_task(parent_trace) 透传到 task_contexts_`
- [ ] 3.2.2 (verify) `ctest -R test_cognitive_worker` 10/10 PASS (9 baseline + 1 新增)
- [ ] 3.2.3 (verify) 全量 ctest 228+8 = 236 PASS 零回归

## 4. T5 DomainWorkerPool::DomainTask.parent_trace (~2h)

### 4.1 DomainTask 字段添加

- [ ] 4.1.1 `include/agenticdsl/cognitive/domain_worker_pool.h` — `DomainTask` struct 加 `std::optional<std::string> parent_trace;` 字段
- [ ] 4.1.2 `src/modules/cognitive/domain_worker_pool.cpp` — worker 发射 `started/completed` 事件时透传 `parent_trace`

### 4.2 测试扩展

- [ ] 4.2.1 `tests/test_domain_worker_pool.cpp` 新增 case: `DomainTask(parent_trace) 透传`
- [ ] 4.2.2 (verify) `ctest -R test_domain_worker_pool` 全绿 (7 baseline + 1 新增)
- [ ] 4.2.3 (verify) 全量 ctest 228+9 = 237 PASS 零回归

## 5. T8 跨 Worker 因果链集成测试 (~5h)

### 5.1 集成测试用例

- [ ] 5.1.1 `tests/test_causal_ordering.cpp` Section D — 3 cases
  - CognitiveWorker A→B 真实因果链 (用 `wait_for_drain()` 同步, `causal_order(evt_a, evt_b) == ABeforeB`)
  - DomainWorkerPool A→B 真实因果链 (同上)
  - TSan 跨 Worker soak (3 producers × 100 tasks, A→B 链路, 用 causal_order 断言所有 evt_a 都在 evt_b 前)

### 5.2 TSan/ASan 验证

- [ ] 5.2.1 (verify) `cmake --preset tsan -DAGENTICDSL_BUILD_TESTS=ON && ctest -R test_causal_ordering` 10/10 PASS + 0 warnings
- [ ] 5.2.2 (verify) `cmake --preset asan -DAGENTICDSL_BUILD_TESTS=ON && ctest` 全绿
- [ ] 5.2.3 (verify) `ctest` 全量 228+12 = 240 PASS 零回归

## 6. Ship + ADR 状态同步 (~1h)

### 6.1 Atomic commits

- [ ] 6.1.1 commit 1: `feat(causal-ordering): add causal_order.h + T7 pure function tests` (Steps 1)
- [ ] 6.1.2 commit 2: `feat(causal-ordering): add ToolResult::parent_trace field + serialization` (Step 2)
- [ ] 6.1.3 commit 3: `feat(causal-ordering): propagate parent_trace through CognitiveWorker` (Step 3)
- [ ] 6.1.4 commit 4: `feat(causal-ordering): propagate parent_trace through DomainWorkerPool` (Step 4)
- [ ] 6.1.5 commit 5: `test(causal-ordering): cross-worker integration + TSan verification` (Step 5)

### 6.2 文档同步

- [ ] 6.2.1 `docs/adr/adr-0037-causal-ordering-impl-scope.md` 更新: T2 余量 / T4 / T5 / T6 / T7 / T8 ✅ shipped (已 ship 部分保持 ✅)
- [ ] 6.2.2 ADR-0037 状态 🟡 Partial 保持 (分布式向量时钟仍 defer — 转 ✅ 条件 per impl-scope)
- [ ] 6.2.3 `docs/active-status.md` §一 Approved 计数不动 + §四 顺延项追加 "ADR-0037 T2/T4-T8 完成" 行
- [ ] 6.2.4 `tools/adr_relationships.py` 重跑生成 relationships.md

### 6.3 归档

- [ ] 6.3.1 (verify) `openspec validate --strict` PASS
- [ ] 6.3.2 (verify) `tools/adr_lint.py` exit 0
- [ ] 6.3.3 (verify) `tools/docs_drift_audit.py` 0 DRIFT items
- [ ] 6.3.4 `mv openspec/changes/2026-09-10-adr-0037-causal-ordering-completion/ openspec/changes/archive/`

## 7. 升级触发 (不适用)

不适用 — 本 change 完成 ADR-0037 剩余 ship，不升级状态。升级到 ✅ Approved 的条件 (per `docs/adr/adr-0037-causal-ordering-impl-scope.md` "结论"):

- 跨进程/分布式 EventBus 落地 (ADR-0046/0059 系列)
- VectorClock 或等价定序机制实施

## 8. Validation Per Step

- [ ] 8.1 cmake --build 零 error + 零 warning
- [ ] 8.2 openspec validate --strict PASS
- [ ] 8.3 adr_lint PASS (68 ADR)
- [ ] 8.4 docs_drift_audit: 0 DRIFT items
- [ ] 8.5 check-llm-default-cleared OK (无 LLM provider 改动)
- [ ] 8.6 TSan preset PASS (跨 Worker 测试零 race)
- [ ] 8.7 ASan preset PASS (0 memory error)

## 总估时

| Step | 内容 | 估时 |
|------|------|:----:|
| 1 | T6 + T7 纯函数 | 6h |
| 2 | T2 余量 + 序列化测试 | 1h |
| 3 | T4 CognitiveWorker | 3h |
| 4 | T5 DomainWorkerPool | 2h |
| 5 | T8 跨 Worker 集成 + TSan | 5h |
| 6 | Ship + ADR sync + archive | 1h |
| **总计** | | **18h** |

**单 session 内可完成** (1.5-2 工作日)。无需 24h cooling-off 拆分 (5 commits 独立可验证)。

## 关联

- `docs/adr/adr-0037-causal-ordering.md` (parent ADR)
- `docs/adr/adr-0037-causal-ordering-impl-scope.md` (impl-scope audit)
- `openspec/changes/archive/adr-0037-causal-clock/` (Phase 1 ship — L1 已 ship)
- `include/agenticdsl/contract/causal_clock.h` (L1 已 ship, 本次不动)
- `tests/test_causal_clock.cpp` (L1 既有 5 cases, 本次不动)