# 真实 3 模型 Baseline 重测 Runbook (Sprint 25 Change #5 + phase-7a-baseline-retest-v2)

> **目的**: Evidence Gate Conditional → PASS 的唯一可代码解锁路径
> **Status**: 🔄 Active — **脚本工具链已 ship (2026-09-04, `phase-7a-baseline-retest-v2` change)**, 等待 §1 触发信号
> **Owner**: Solo Dev (Single-Developer Mode)
> **最后更新**: 2026-09-04

---

## §1 触发信号 (Trigger Signal)

**模型窗口** = 3 个目标模型（claude-opus-4.5 / kimi-k2.6 / gpt-5）均可调用 + Solo Dev 时间预算允许 ≤8h 中断。

**窗口开**的判定方式（per Oracle Decision 1）:

```bash
# 触发信号文件存在检查 + 时间戳 ≤7 天
test -f docs/audits/$(date +%Y-%m-%d)-baseline-window-open.md && \
  find docs/audits/$(date +%Y-%m-%d)-baseline-window-open.md -mtime -7
```

**满足条件**:
- `docs/audits/<date>-baseline-window-open.md` 文件**存在**（含判定说明 + 责任人签字）
- 文件**修改时间 ≤7 天**（避免过期信号）

**创建触发信号文件**:
```bash
# 责任人手动创建（per self-review）
cat > docs/audits/$(date +%Y-%m-%d)-baseline-window-open.md <<EOF
# Baseline 重测窗口开放

- **Date**: $(date +%Y-%m-%d)
- **判定**: Solo Dev (per 27h/周容量 + 当前 Sprint 容量评估)
- **窗口期**: 2026-XX-XX ~ 2026-XX-XX (≤14 天)
- **3 模型可用性**: claude-opus-4.5 ✅ / kimi-k2.6 ✅ / gpt-5 ✅
- **API 配额**: 足够 600 样本测量 + retry ≤3 次
EOF
```

---

## §2 重测 Runbook (Baseline Measurement)

**入口命令**（per `phase-7a-baseline-retest-v2` change 2026-09-04 ship 的脚本工具链）:

```bash
# 1. 环境检查（依赖 + 模型可达）
python3 scripts/measure-baseline.py --check-env \
  --models claude-opus-4.5,kimi-k2.6,gpt-5 --mode real

# 2. 全量测量（3 模型 × 4 维度 × 54 tasks × 3 prompts = 1944 样本, --concurrency 默认 3）
python3 scripts/measure-baseline.py \
  --models claude-opus-4.5,kimi-k2.6,gpt-5 \
  --mode real \
  --prompts v1_schema,v2_fewshot,v3_two_phase \
  --dimensions structured,tool_call,error_recovery,long_context \
  --tasks-dir lib/prompt/golden/ \
  --output docs/baselines/baseline-real-$(date +%Y-%m-%d).json

# 3. 输出 JSON schema 校验
python3 tools/baseline_schema_validate.py \
  docs/baselines/baseline-real-$(date +%Y-%m-%d).json
```

**预期输出**:
- `docs/baselines/baseline-real-<date>.json` 含 1944 个样本的 parse-valid + task-success L1/L2/L3 数据 (3 models × 4 dims × 54 tasks × 3 prompts)
- 校验通过（exit 0）

---

## §3 Evidence Gate 重跑 (Per ADR-0074 D4)

**入口命令**:

```bash
# 决议脚本（per phase-7a-baseline-retest-v2 change, bash 脚本用 bash 调用）
bash scripts/evidence-gate-v1.sh \
  docs/baselines/baseline-real-$(date +%Y-%m-%d).json \
  --write-markdown docs/audits/$(date +%Y-%m-%d)-evidence-gate-v1.md
```

**预期输出** (per evidence_gate.h + runbook §3 三带判定, 完整 4 维 FAIL 表 per Oracle B2):

| 决议 | 触发条件 |
|------|----------|
| **PASS** | parse-valid ≥90% AND task-success L1 ≥70% AND L2 ≥50% AND L3 ≥30% (3 模型平均) |
| **Conditional** | parse-valid ∈ [85, 90) AND task-success L1 ≥70% (临界带; L2/L3 仅记录) |
| **FAIL** | parse-valid <85% OR L1 <70% OR (parse-valid ≥90% AND (L2 <50% OR L3 <30%)) |

> **Mock mode 注**: `mock_mode: true` 的 baseline JSON 输入时, verdict 强制 Conditional (占位语义, 不构成真实 PASS — 防 control-plane-eval.py C5 误消费).

**决议结果处理**:

| 决议 | 后续动作 |
|------|----------|
| **PASS** | 更新 ADR-0074 状态 ✅ Approved + active-status.md + Phase 7a C5 自动转 PASS |
| **Conditional** | 与 mock Conditional 一致（保持 🟡 Conditional）+ 重测计划待定 |
| **FAIL** | 记录失败原因 + 重测计划调整（可能触发 ADR-0072 D2 `$var` Conditional ship 路径） |

---

## §4 容量预算 + 失败模式 (Budget & Fallback)

### 容量预算

| 项 | 时间预算 |
|----|----------|
| 全量测量（600 样本） | ≤6h |
| Evidence Gate 重跑 | ≤5min |
| 失败重试 + 诊断 | ≤1.5h |
| **合计** | **≤8h** |

### 失败模式 Fallback

| 失败模式 | 检测信号 | Fallback |
|---------|---------|----------|
| **模型 API 5xx** | HTTP 5xx / timeout | 自动重试 ≤3 次（指数退避 30s/60s/120s）后放弃该样本，记录到 baseline JSON `errors` 字段 |
| **测量超时 (>8h)** | wall-clock > 28800s | 拆分 4 维度为 4 个独立任务（structured / tool_call / error_recovery / long_context），每个 ≤2h，分 2 sprint 完成 |
| **Evidence Gate 脚本不存在** | `scripts/evidence-gate-v1.sh` 文件缺失 | 中止重测 + 创建 follow-up change + 留待 Sprint 26 实施 |
| **3 模型中 1 个不可用** | API 健康检查失败 | 降级为 2 模型（保留 parse-valid 数据，task-success 标注 `models_partial: ["X", "Y"]`），Evidence Gate 决议标注 "PARTIAL_MODELS" |
| **JSON 输出 schema 不匹配** | `tools/baseline_schema_validate.py` exit ≠ 0 | 修复测量脚本 + 重跑全量 |

---

## §5 相关文档链接

- **Evidence Gate 决议源**: [`docs/audits/2026-09-02-evidence-gate-v1.md`](../audits/2026-09-02-evidence-gate-v1.md)
- **ADR-0074 D4 阈值**: [`docs/adr/adr-0074-prompt-evidence-gate.md`](../adr/adr-0074-prompt-evidence-gate.md) §决策 D4
- **Phase 6c baseline 工具**: [`openspec/changes/archive/2026-08-18-from-roadmap-phase-6c-execution-baseline/`](../../openspec/changes/archive/2026-08-18-from-roadmap-phase-6c-execution-baseline/)
- **Phase 6c evidence-gate change**: [`openspec/changes/archive/2026-09-01-from-roadmap-phase-6c-evidence-gate/`](../../openspec/changes/archive/2026-09-01-from-roadmap-phase-6c-evidence-gate/)
- **重测脚本工具链 (2026-09-04 ship)**: [`phase-7a-baseline-retest-v2 change`](../../openspec/changes/archive/2026-09-04-phase-7a-baseline-retest-v2/) — `scripts/measure-baseline.py` + `scripts/evidence-gate-v1.sh` + `tools/baseline_schema_validate.py`
- **Sprint 25+ 排期**: [`roadmap.md`](../../roadmap.md) §Phase 7a 启动条件复评触发点

---

## §6 复评触发点 (Phase 7a C5 解锁路径)

按 `roadmap.md` Q2b 决策树：

1. **触发信号**（§1）+ 重测执行（§2）= baseline-real-<date>.json
2. **Evidence Gate 重跑**（§3）= PASS / Conditional / FAIL (用 `bash scripts/evidence-gate-v1.sh ... --write-markdown`)
3. **决议 PASS** → `--write-markdown` 生成 `docs/audits/<date>-evidence-gate-v1.md` 含 `Verdict: PASS` → `control-plane-eval.py` C5 自动转 PASS → 更新 ADR-0074 + active-status.md
4. **决议 Conditional/FAIL** → 保持 Conditional + 重测计划调整（回 §2 拆分维度跑 或 等下一窗口）

**Phase 7a 6 项条件复评**：per `scripts/control-plane-eval.py --dry-run --relaxed`（per Change #2 `--relaxed` 模式让 C2 不阻塞决策）。
