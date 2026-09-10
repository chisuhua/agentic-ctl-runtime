## ADDED Requirements

### Requirement: causal_order() 判定函数 (T6)

`agenticdsl::event::causal_order(const BusEvent& a, const BusEvent& b)` SHALL 返回两个事件之间的因果关系，按 L2 → L1 → 默认优先级判定：

1. **L2 优先**: 若 `a.payload.trace_id` 等于 `b.payload.parent_trace` → `CausalRelation::ABeforeB`；反向同理（`trace_id`/`parent_trace` 是 ToolResult 字段，在 `BusEvent.payload` 内）
2. **L1 回退**: 若两者 `causal_time` 都非 0 → `causal_time` 小者先
3. **默认**: 否则 → `CausalRelation::Concurrent`

判定函数 SHALL 仅判 1-hop 直接因果（ADR-0037 line 538 注明），传递性由调用方链式推导。

#### Scenario: L2 显式因果链匹配
- WHEN `BusEvent a` 的 `payload.trace_id == "task-A-123"`
- AND `BusEvent b` 的 `payload.parent_trace == "task-A-123"`
- THEN `causal_order(a, b) == CausalRelation::ABeforeB`
- AND `happens_before(a, b) == true`
- AND `happens_before(b, a) == false`

#### Scenario: L1 causal_time 回退
- WHEN `a.causal_time == 10` AND `a.payload.parent_trace` 为空
- AND `b.causal_time == 20` AND `b.payload.parent_trace` 为空
- AND `a.payload.trace_id == "x"` AND `b.payload.trace_id == "y"`（无 L2 匹配）
- THEN `causal_order(a, b) == CausalRelation::ABeforeB`

#### Scenario: L1 causal_time 反向
- WHEN `a.causal_time == 20` AND `b.causal_time == 10`
- AND 无 L2 匹配
- THEN `causal_order(a, b) == CausalRelation::BBeforeA`

#### Scenario: causal_time == 0 sentinel 处理
- WHEN `a.causal_time == 0` AND `b.causal_time == 0`
- AND 无 L2 匹配
- THEN `causal_order(a, b) == CausalRelation::Concurrent`
- AND 不触发"L1 回退"误判为 ABeforeB

#### Scenario: 同 trace_id 无 parent_trace
- WHEN `a.payload.trace_id == b.payload.trace_id == "task-X"`（重复事件）
- AND `a.payload.parent_trace == std::nullopt` AND `b.payload.parent_trace == std::nullopt`
- THEN `causal_order(a, b) == CausalRelation::Concurrent`

#### Scenario: 传递性（调用方链式推导）
- WHEN 构造 `a.payload.trace_id = "A"`, `b.payload.parent_trace = "A"`, `b.payload.trace_id = "B"`, `c.payload.parent_trace = "B"`
- THEN `causal_order(a, b) == ABeforeB` AND `causal_order(b, c) == ABeforeB`
- AND 调用方手动链式推导: `causal_order(a, c) == ABeforeB`（当 c.payload.parent_trace != a.payload.trace_id 时需经 b 中介）

### Requirement: ToolResult parent_trace 字段 + 序列化 (T2 余量)

`ToolResult` SHALL 增加 `std::optional<std::string> parent_trace` 字段，承载 L2 因果链。JSON 序列化 SHALL 输出顶层 `"parent_trace"` 字段（缺值时不输出 key）。

#### Scenario: 字段存在 round-trip
- WHEN `ToolResult t; t.parent_trace = "task-A-123";`
- THEN `t.to_json()` 输出 `"parent_trace": "task-A-123"`
- AND `ToolResult::from_json(t.to_json()).parent_trace == "task-A-123"`

#### Scenario: 缺值容错（向后兼容）
- WHEN 旧 JSONL 数据无 `parent_trace` 字段（来自未 ship 前的持久化）
- THEN `ToolResult::from_json(json).parent_trace == std::nullopt`
- AND 不抛异常

### Requirement: CognitiveWorker parent_trace 透传 (T4)

`CognitiveWorker::submit_task(task_id, prompt, parent_trace = std::nullopt)` SHALL 接受 `std::optional<std::string>` 默认参数。Worker 在发射事件时 SHALL 将 `parent_trace` 透传到事件 `payload.parent_trace`（**顶层 ToolResult 字段**，与既有 `trace_id` 同模式 — per design Decision 5）。

#### Scenario: 默认参数零迁移
- WHEN 现有调用方调用 `submit_task("task-1", "prompt")`（2 参数旧形式）
- THEN 编译通过，运行时 `parent_trace == std::nullopt`
- AND 既有 `tests/test_cognitive_worker.cpp` 9 cases 全部零修改 PASS

#### Scenario: 显式 parent_trace 存储到 task_contexts_
- WHEN 调用 `submit_task("task-A", "p", "task-P")`
- THEN Worker 内部 `task_contexts_["task-A"].parent_trace == "task-P"`
- AND 后续 worker emit 事件时透传此因果链

### Requirement: DomainWorkerPool parent_trace 透传 (T5)

`DomainTask` 结构体 SHALL 增加 `std::optional<std::string> parent_trace` 字段。`DomainWorkerPool::submit_task(DomainTask)` SHALL 在 Worker 发射事件时透传 `parent_trace` 到事件 payload。

#### Scenario: DomainTask.parent_trace 默认缺省
- WHEN 现有调用方构造 `DomainTask{...}`（不指定 parent_trace）
- THEN 编译通过，运行时 `task.parent_trace == std::nullopt`
- AND 既有 `tests/test_domain_worker_pool.cpp` 7 cases 全部零修改 PASS

#### Scenario: 显式 parent_trace 发射事件透传
- WHEN 调用 `submit_task(DomainTask{..., parent_trace = "task-P"})`
- THEN Worker 后续 emit 的事件 payload 包含 `parent_trace == "task-P"`

### Requirement: 跨 Worker 因果链端到端测试 (T8)

`tests/test_causal_ordering.cpp` Section D SHALL 覆盖跨 CognitiveWorker / DomainWorkerPool 的真实因果链：通过 InMemoryBus 捕获事件，用 `causal_order()` 验证 A→B 因果关系。测试 SHALL 在 TSan preset 下零 data race。

#### Scenario: CognitiveWorker A→B 因果链
- WHEN 提交任务 A (parent_trace = std::nullopt)
- AND A 完成后 Worker-A 触发任务 B (parent_trace = A.trace_id)
- AND 用 `InMemoryBus::wait_for_drain()` 同步
- THEN 从 bus 订阅事件，捕获 evt_a (A 完成) 和 evt_b (B 启动)
- AND `causal_order(evt_a, evt_b) == CausalRelation::ABeforeB`
- AND `evt_a.causal_time < evt_b.causal_time`

#### Scenario: DomainWorkerPool A→B 因果链
- WHEN 提交 DomainTask A (parent_trace = std::nullopt)
- AND A 完成后 Worker 触发 DomainTask B (parent_trace = A.task_id)
- AND `wait_for_drain()` 同步
- THEN `causal_order(evt_a, evt_b) == CausalRelation::ABeforeB`

#### Scenario: TSan 干净
- WHEN 跑 `cmake --preset tsan -DAGENTICDSL_BUILD_TESTS=ON && ctest -R test_causal_ordering`
- THEN 12 cases 全部 PASS
- AND 0 TSan warnings（跨 Worker bus dispatch + parent_trace 透传链路零 race）

#### Scenario 注脚: A→B→C 链式因果正确性覆盖范围
- ADR-0037 §验证标准"因果链正确性"原文要求"A→B→C 链式任务,验证 parent_trace 传递正确"
- 本 Requirement (T8) 跨 Worker 验证仅覆盖 **A→B 因果传播链路**（Worker A emit → Worker B 收到 parent_trace 触发）
- **A→B→C 链式因果的传递性** 由 Requirement 1 (T6) Scenario "传递性（调用方链式推导）" 通过纯函数合成 BusEvent 链式调用验证（a.payload.trace_id/b.payload.parent_trace/b.payload.trace_id/c.payload.parent_trace 三跳链接）
- 不增加跨 Worker A→B→C case 的理由: 链路同构（A→B 已验证 A→X 透传；A→B→C 只需 b→c 一次同样透传），传递性由纯函数判定函数本身承担；测试冗余度低，省 1h 估时

### Requirement: 回归守卫

现有 228/228 ctest baseline SHALL 保持全绿。本 change 新增 12 cases (`test_causal_ordering` + `test_cognitive_worker` +1 + `test_domain_worker_pool` +1)，总计 240/240 ctest PASS。

#### Scenario: 全量 ctest 零回归
- WHEN 任意 Step (1-5) ship 后跑 `ctest`
- THEN 全量 ctest PASS
- AND `cmake --preset asan && ctest` 全绿
- AND `cmake --preset tsan && ctest` 全绿
- AND `openspec validate --strict` PASS
- AND `tools/adr_lint.py` exit 0
- AND `tools/docs_drift_audit.py` 0 DRIFT items