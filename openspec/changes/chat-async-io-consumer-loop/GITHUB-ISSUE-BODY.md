## Change 概述

<!-- 1-2 段说明本 OpenSpec change 是什么、解决什么问题、影响哪些 Gap -->

**Change 路径**: `openspec/changes/chat-async-io-consumer-loop/`
**状态**: 🔍 Proposed → ✅ Approved (autonomous prep complete, awaiting 24h cooling-off)
**关联 Gap**: 现场 bug 复现 2026-09-07 15:54:38 — 用户报告 `pdk_chat_demo` 发出第一轮问题后未看到云端回复，第二轮发出后卡死
**关联 chat-async 系列**:
- `chat-async-queue-infra` (d4fcca1) producer 已 ship，但 consumer 从未 ship
- `chat-async-cancellation-chain` (d1ecca2) stop_token 链路已 ship
- `chat-async-io-model-switching` (526c88b) /model 命令已 ship
- `c30b2b3` isatty 守卫 partial fix — 本 change 完整撤回（single-reader 模式消除了 race）
**Oracle session R4**: `ses_f83a3299cffe4iP6NPeRXPbNja` (CONDITIONAL PASS → 1 行 spec fix 后 PASS)

---

## Self-Review Checklist (12+4 项)

<!-- 按 OpenSpec change 实际内容定制决策点 -->

### A. 设计完整性 (4/4 项)
- [ ] **A1. 背景与上下文**: `proposal.md` §Why 完整描述 stdin 双读 race + dead-producer bug；与 chat-async-queue-infra producer-only ship 状态的关系清晰
- [ ] **A2. 决策 (Decision)**: `design.md` 含 7 个 Decision（Single-reader pattern / Condition Variable / Steering Interrupt / ~~Follow-Up Drain~~ SUPERSEDED / 撤销 isatty / 复用 stop_input_thread_ / SINGLE SHARED CancellationRegistry）；每项有方案 + 理由 + 替代方案 + 代码示例
- [ ] **A3. 不变量 (Invariants)**: `design.md` Decision 2 显式不变量表（count == sum(queue sizes)）；R4e 修复 §2.7 删除"重置为 0"错误备选；spec Req 1-6 各自断言不变量
- [ ] **A4. 触发条件**: `tasks.md` §4.0 显式标注 "🚨 P0 任务。必须在本节所有 4.x 子任务开始前完成 4.0"；§5 标注 "⚠️ 依赖：Task 4.0 已 ship"；§10.9 标注 "requires Task 4.0 shipped"

### B. 风险与备选 (4/4 项)
- [ ] **B1. 风险评估**: `design.md` Risks / Trade-offs 表格含 8 项；R4-fix 后 NH2 EOF shutdown 已有 task §3.4 落地
- [ ] **B2. 备选方案**: `design.md` Decision 1/2/7 各含 ≥2 项备选；Decision 7 列出 3 项备选（registry 指针传递 vs 移入 loop_agent vs 全局共享）
- [ ] **B3. 反向影响**: 26+ ChatSession 构造 call site（9 个测试文件 + main.cpp:446/626）；`tasks.md` §4.0.9 加默认值 `= nullptr` + Impl self-owned fallback + §4.0.14 AC 验证编译通过；NC3 修复后零回归
- [ ] **B4. 安全/合规评估**: ⏸ N/A（本 change 不涉及 PII/凭据/隐私决策，仅 stdin 读取 + queue 调度 + cancellation token）

### C. 实施与依赖 (5/5 项)
- [ ] **C1. 实施计划**: `tasks.md` 含 14 个 task group（§1-§11 + §3.4 NH2 + §4.0 C1/C2 + §4.0.9-§4.0.14 NC3/NH3 + §5.2 pop-on-hit + §7.7 /cancel 注册 + §10.5-§10.10 手动验证）
- [ ] **C2. 依赖关系**: chat-async-queue-infra (d4fcca1) / cancellation-chain (d1ecca2) / model-switching (526c88b) 已 ship；c30b2b3 isatty 守卫 — R4 撤回
- [ ] **C3. 契约层一致性**: 新增 3 个 ChatSession public API（`try_pop_input` / `pop_next_input` / `try_peek_input`）+ InputMessage struct；唯一签名变更加默认值 `= nullptr`；与 `agenticdsl/contract/*` 协调
- [ ] **C4. 文档同步**: capability-application-map-2026-08.md 不需要同步（operational bug fix）；examples/pdk_chat_demo/README.md 需更新（tasks §10.10）；active-status.md 应在 archive 后更新
- [ ] **C5. ADR-TRACKING-01**: ⏸ N/A（本 change 不建立新 ADR，实施对象是 OpenSpec change + 修订既有 ADR-0068）

### 2.1 接口契约类（专用 — 本 change 主要新增 ChatSession public API）
- [ ] **D1. 接口位置**: 与现有 `pdk_chat_demo::ChatSession` 同一 namespace；header 内联声明
- [ ] **D2. V1 范围**: 简化 3 方法 + InputMessage struct + atomic counter；不引入 Meta API
- [ ] **D3. 依赖类型**: 引用 `std::chrono::milliseconds`、`std::optional`、`std::atomic<size_t>` — 全部 C++20 标准库
- [ ] **D4. 与现有类型关系**: 扩展：与既有 `try_push_*_for_test` / `try_clear_queue` 配套（test-only）；与 `chat()` API 互补（push/pull 两端）

---

## 自审决策（24h Cooling-Off 后填写）

<!-- 创建 issue 后 24h 才能填写；期间如发现新问题则更新此节 -->

**Change 创建时间**: 2026-09-07 16:39:45 CST
**当前时间**: 2026-09-07 23:10:39 CST
**已过冷却期**: 6.52h / 24h（17.48h remaining）
**最早 ship 时间**: 2026-09-08 16:39:45 CST

### Oracle 审查证据链（4 轮）

| Round | Session | 结果 | 关键发现 |
|---|---|---|---|
| R1 | `ses_f84ea6dbcffexB4Q1cqx1GQE3J` | FAIL | C1/C2/C3 |
| R2 | `ses_f83d04ddeffe5XKDqc4zR9fy82` | FAIL | NC1/NC2/NC3/NH1/NH2/NH3 |
| R3 | `bg_6944ace5` | FAIL CONDITIONAL | 5 R3 ship-blockers |
| R4 | `ses_f83a3299cffe4iP6NPeRXPbNja` | CONDITIONAL PASS → **PASS** | 5 项 FIXED + 1 行 spec fix |

### 最终状态判定（cooling-off 后填写）

| 决策 | 触发条件 | 当前评估 |
|---|---|---|
| ✅ **Approved** | 12 项 + 4 项专用清单全过 | ✅ 11/12 + 1 N/A (B4) + 1 待 user 决策 (C4) |
| ❌ **Rejected** | 任一项不通过 + 无修改空间 | ❌ 不适用 |
| ⏸ **Deferred** | 前置未达 | ❌ 不适用 |

**建议决策**: ✅ **Approved**

---

## Ship + 实施命令序列（cooling-off 过期后执行）

```bash
cd /workspace/project/HydraForge

# 1. 复查 Git 状态
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

# 4. 实施（按 tasks.md §1 → §11 顺序）
/opsx-apply

# 5. ship gate（tasks.md §10.5-§10.10 手动验证 + ctest baseline 184/185 不回归）
```

---

## 关联证据链

| 证据 | 路径 / Session |
|---|---|
| 现场 bug 复现（用户报告） | 2026-09-07 15:54:38 日志 — `user.input: hat can you do?` 首字符被吞 |
| OpenSpec validation | `openspec validate chat-async-io-consumer-loop --strict` → "Change is valid" |
| Status check | 4/4 artifacts complete |
| Self-Review Checklist | `openspec/changes/chat-async-io-consumer-loop/SELF-REVIEW-CHECKLIST.md` |

---

**关联 issue body 模板**: `.github/ISSUE_TEMPLATE/adr-review.md`
**关联 OpenSpec change**: `openspec/changes/chat-async-io-consumer-loop/`
