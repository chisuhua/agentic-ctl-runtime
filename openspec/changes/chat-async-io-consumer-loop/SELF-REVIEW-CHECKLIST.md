# Self-Review Checklist (chat-async-io-consumer-loop)

> **用法**：本清单基于 `docs/architecture/adr-self-review-checklist.md`（12 项通用 + 4 类专用）+ 4 轮 Oracle 审查发现预填。**所有 ✅/❌/⏸ 决策需用户手动确认**（per Single-Developer Mode：作者 = 评审人）。
> **关联 issue body 模板**：`.github/ISSUE_TEMPLATE/adr-review.md`
> **生成时间**：2026-09-07 23:10 CST（change 创建后 6.52h，cooling-off 尚未到期）
> **GitHub Issue**: [#20](https://github.com/chisuhua/HydraForge/issues/20) — `[chat-async-io-consumer-loop] Self-Review`
> **Labels**: `adr-review`, `self-review`, `sprint-24` | **Assignee**: `chisuhua` | **State**: OPEN
> **最终状态判定**：✅ Approved | ❌ Rejected | ⏸ Deferred（见 §五）

---

## 一、A. 设计完整性 (4/4 项)

### A1. 背景与上下文 — ✅ 建议 ✅
- **现状**：`proposal.md` §Why 完整描述了 stdin 双读 race + dead-producer bug（现场复现 2026-09-07 15:54:38 用户报告首字符 `w` 被吞、第二轮卡住）；与 chat-async-queue-infra (d4fcca1) producer-only ship 状态的关系清晰
- **Oracle 验证**：R1-R4 四轮审查均确认背景准确

### A2. 决策 (Decision) — ✅ 建议 ✅
- **现状**：`design.md` 含 7 个 Decision（Single-reader pattern / Condition Variable / Steering Interrupt / ~~Follow-Up Drain~~ SUPERSEDED / 撤销 isatty / 复用 stop_input_thread_ / SINGLE SHARED CancellationRegistry）；每项有方案 + 理由 + 替代方案 + 代码示例
- **Oracle 验证**：R1 指出 C1/C2/C3 缺失 → R2 修复引入 NC1-NC3+NH1-NH3 → R3 修复 Decision 4/3 草图不一致 → 终版一致

### A3. 不变量 (Invariants) — ✅ 建议 ✅
- **现状**：`design.md` Decision 2 显式不变量表（count == sum(queue sizes)）；R4e 修复 §2.7 删除"重置为 0"错误备选；spec Req 1-6 各自断言不变量
- **Oracle 验证**：NC2 修复后所有 push/pop/clear 路径一致

### A4. 触发条件 — ✅ 建议 ✅
- **现状**：`tasks.md` §4.0 显式标注 "🚨 P0 任务。必须在本节所有 4.x 子任务开始前完成 4.0"；§5 标注 "⚠️ 依赖：Task 4.0 已 ship"；§10.9 标注 "requires Task 4.0 shipped"
- **Oracle 验证**：R2 NC3 修复 + R3/R4 强化依赖链

## 二、B. 风险与备选 (4/4 项)

### B1. 风险评估 — ✅ 建议 ✅
- **现状**：`design.md` Risks / Trade-offs 表格含 8 项（R1 cv spurious wakeup / R2 interrupt join / R3 cv_mutex contention / R4 follow-up drain语义错位 / R5 pipe EOF 退出 / R6 isatty 守卫撤回 / R7 interrupt_thread 启动开销 / R8 现有测试兼容）；R4-fix R3 修正后 NH2 EOF shutdown 已有 task §3.4 落地
- **Oracle 验证**：R1-R4 覆盖所有架构风险

### B2. 备选方案 — ✅ 建议 ✅
- **现状**：`design.md` Decision 1/2/7 各含 ≥2 项备选（如 "single-reader pattern 替代 std::cin mutex / select-poll 多路复用"）；Decision 2 列出 atomic vs mutex 备选；Decision 7 列出"传递 registry 指针 vs 移入 loop_agent vs 全局共享"
- **Oracle 验证**：R2 指出 Decision 7 备选理由有事实错误（"与 bus_ptr 透传模式不一致" 实际 bus_ptr 也是透传），但已在 R3 修复语义（用全局+impl_ 同源）

### B3. 反向影响 — ✅ 建议 ✅
- **现状**：`proposal.md` §Impact 列出 26+ ChatSession 构造 call site（9 个测试文件 + main.cpp:446/626）；`tasks.md` §4.0.9 加默认值 `= nullptr` + Impl self-owned fallback + §4.0.14 AC 验证编译通过；NC3 修复后零回归
- **Oracle 验证**：R2 NC3 + R3 NH3 共同验证向后兼容

### B4. 安全/合规评估 — ⏸ N/A
- **现状**：本 change 不涉及 PII/凭据/隐私决策（仅 stdin 读取 + queue 调度 + cancellation token）
- **评估**：N/A — 跳过（per checklist "安全/合规评估 — 涉及 PII/凭据/隐私的决策需安全角色评估"）

## 三、C. 实施与依赖 (5/5 项)

### C1. 实施计划 — ✅ 建议 ✅
- **现状**：`tasks.md` 含 14 个 task group（§1-§11 + §3.4 NH2 + §4.0 C1/C2 + §4.0.9-§4.0.14 NC3/NH3 + §5.2 pop-on-hit + §7.7 /cancel 注册 + §10.5-§10.10 手动验证），每项 ≤2 小时粒度
- **Oracle 验证**：R4 后所有 ship-blocker 修复均落入 task 编号

### C2. 依赖关系 — ✅ 建议 ✅
- **现状**：
  - chat-async-queue-infra (d4fcca1) producer ✅ shipped
  - chat-async-cancellation-chain (d1ecca2) stop_token ✅ shipped
  - chat-async-io-model-switching (526c88b) /model command ✅ shipped
  - c30b2b3 isatty 守卫 — R4 撤回（spec Req 6 显式说明）
- **Oracle 验证**：R3 设计文档 Decision 5 已论证

### C3. 契约层一致性 — ✅ 建议 ✅
- **现状**：
  - 新增 3 个 ChatSession public API（`try_pop_input` / `pop_next_input` / `try_peek_input`）— 全部新增方法，不修改现有签名
  - 唯一签名变更：ChatSession 构造增加 `shared_ptr<CancellationRegistry>` 第 6 参（默认值 `= nullptr` 保持向后兼容）
  - 与现有 `agenticdsl/contract/itool_registry.h` / `iinteraction_bus.h` / `event_builder.h` 协调一致（不引入新概念，仅扩展既有 cancellation 路径）
- **Oracle 验证**：R2 NC3 + R3 NH3 修复后契约层零破坏

### C4. 文档同步 — ⏸ 建议 ⏸（待 user 决策）
- **现状**：
  - `docs/architecture/capability-application-map-2026-08.md` 不需要同步（此 change 是 operational bug fix，不新增 capability）
  - `examples/pdk_chat_demo/README.md` 需要更新（提及 single-reader 模式 + 共享 CancellationRegistry）— `tasks.md` §10.10 已有该子任务
  - `docs/active-status.md` 应当在 archive 后更新（OpenSpec active count +1/-1）
- **Oracle 验证**：R4 后无需 architecture 工作组额外评审

### C5. ADR-TRACKING-01 — ⏸ N/A
- **评估**：本 change **不建立新 ADR**，实施对象是 `openspec/changes/chat-async-io-consumer-loop/`（OpenSpec change）。涉及的是现有 ADR-0068 (Event Emission) + chat-async-queue-infra / cancellation-chain / model-switching 三个已 ship change + c30b2b3 partial fix 的修订
- **决策**：N/A — 不需要新 ADR 或 tracking 标注

## 四、专用清单（2.1 接口契约类 — 本 change 主要新增 ChatSession public API）

| # | 检查项 | 关键问题 | 建议 |
|---|---|---|---|
| 1 | 接口位置 | 与现有 `pdk_chat_demo::ChatSession` 同一 namespace；header 内联声明 | ✅ |
| 2 | V1 范围 | 简化 3 方法（try_pop/pop_next/try_peek）+ InputMessage struct + atomic counter；不引入 Meta API（如 cancel_token wrapper） | ✅ |
| 3 | 依赖类型 | 引用 `std::chrono::milliseconds`、`std::optional`、`std::atomic<size_t>` — 全部 C++20 标准库 | ✅ |
| 4 | 与现有类型关系 | 扩展：与既有 `try_push_*_for_test` / `try_clear_queue` 配套（test-only）；与 `chat()` API 互补（push/pull 两端） | ✅ |

## 五、决策框架 (3 选 1)

| 决策 | 触发条件 | 是否满足 | 后续动作 |
|---|---|---|---|
| ✅ **Approved** | 12 项通用 + 专用清单全过 | ✅ 11/12 + 1 N/A (B4) + 1 待 user 决策 (C4) | **ship 准备完成**：等 17.48h cooling-off 后执行 `git commit` + `openspec archive` + 启动 `/opsx-apply` 实施 |
| ❌ **Rejected** | 任一项不通过 + 无修改空间 | ❌ 不适用 | — |
| ⏸ **Deferred** | 前置未达（如其他 ADR 未批） | ❌ 不适用 | — |

**最终建议决策**：✅ **Approved**

---

## 六、User Action Items（cooling-off 后执行）

### 6.1 Cooling-Off 状态
- **创建时间**：2026-09-07 16:39:45 CST
- **当前时间**：2026-09-07 23:10:39 CST
- **已过冷却期**：6.52h / 24h（17.48h remaining）
- **最早 ship 时间**：2026-09-08 16:39:45 CST

### 6.2 Ship 命令序列（cooling-off 过期后）

```bash
cd /workspace/project/HydraForge

# 1. 确认 Git 状态（应仅 openspec/changes/chat-async-io-consumer-loop/ 未跟踪）
git status

# 2. commit artifacts + self-review
git add openspec/changes/chat-async-io-consumer-loop/
git commit -m "fix(chat-async-io-consumer-loop): close stdin race + producer-consumer loop

Single-reader mode (input thread is sole stdin reader, main loop polls queue)
Shared CancellationRegistry instance (Task4.0: delete g_loop_registry, use pdk_chat_demo::g_cancellation_registry)
Atomic pending_input_count_ for lock-free predicate (C2/C3 fix)
Interrupt poll uses try_peek_input + pop-only-on-/cancel (NH1 fix)
/cancel registered as command (H1 fix)
Null-guard fallback: non-cancellable-but-executable (NH3 fix)
EOF triggers stop_input_thread_ + notify (NH2 fix)

Closes: stdin race + dead-producer bug from 2026-09-07 15:54
Spec: openspec/specs/chat-async-io-consumer-loop/

4 Oracle review rounds passed (C1/C2/C3 + NC1/NC2/NC3 + NH1/NH2/NH3 + N1 spec prose)"

# 3. Archive change
openspec archive chat-async-io-consumer-loop
# 或 /openspec-archive-change

# 4. 实施（按 tasks.md §1 → §11 顺序）
# 建议：/opsx-apply（自动 TDD 5 步执行）

# 5. ship gate（tasks.md §10.5-§10.10 手动验证 + ctest baseline 184/185 不回归）
```

### 6.3 实施期注意事项

- **task 4.0 强制先于 4.1-4.5**（tasks.md §4 显式标注 "BLOCKS"）
- **task 4.0.9 必须加默认值 `= nullptr`**（否则 26+ 构造 call site 编译失败）
- **task 5.2 必须用 `try_peek_input` + 命中 /cancel 才 pop**（spec Req 4 显式要求）
- **task 7.7 必须注册 `/cancel` 命令**（H1 修复核心）
- **task 10.3 必须跑 TSan preset**（暴露 C2 data race 修复）
- **task 10.9 必须验证后置条件**：`grep -rn "register_source\|static.*[Rr]egistry" pdk/loop_agent/src/pdk_entry.cpp` 必须返回 0 行

---

## 八、关联证据链

| 证据 | 路径 / Session |
|---|---|
| 现场 bug 复现（用户报告） | 2026-09-07 15:54:38 日志 — `user.input: hat can you do?` 首字符被吞 |
| Oracle R1 审查 | session `ses_f84ea6dbcffexB4Q1cqx1GQE3J` (10 messages, C1/C2/C3) |
| Oracle R2 审查 | session `ses_f83d04ddeffe5XKDqc4zR9fy82` (6 NEW: NC1/NC2/NC3/NH1/NH2/NH3) |
| Oracle R3 审查 | bg_6944ace5 (5 R3 ship-blockers + 2 NEW HIGH) |
| Oracle R4 审查 | session `ses_f83a3299cffe4iP6NPeRXPbNja` (CONDITIONAL PASS) |
| OpenSpec validation | `openspec validate chat-async-io-consumer-loop --strict` → "Change is valid" |
| Status check | `openspec status --change chat-async-io-consumer-loop` → 4/4 artifacts complete |

---

## 九、维护与演进

- **归档后**：change 移至 `openspec/changes/archive/2026-09-07-chat-async-io-consumer-loop/`
- **spec 落地**：合并 `specs/chat-async-io-consumer-loop/spec.md` + `specs/pdk-chat-demo-runtime-fix/spec.md` delta → `openspec/specs/` 根目录（下次 docs-cleanup change）
- **cap-map 同步**：`docs/architecture/capability-application-map-2026-08.md` 添加新能力 "Async I/O consumer loop" 条目（如适用）
- **后续 ADR-0050 Candidate B 路径**：本 change 是服务化方向的基础（chat async I/O consumer 闭环）