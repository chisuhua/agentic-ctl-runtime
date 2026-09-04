# Evidence Gate v1 Decision Document

> **Date**: 2026-09-02
> **ADR**: [ADR-0074 — Prompt Engineering + Evidence Gate](../adr/adr-0074-prompt-evidence-gate.md) §决策 D4/D5
> **OpenSpec change**: `from-roadmap-phase-6c-evidence-gate`
> **Author**: Solo Dev (Single-Developer Mode)
> **Status**: ⏳ **Conditional placeholder** (MOMUS 修正后 X 路线 ship — 真实 3 模型 baseline 决议推迟至 Sprint 25+)
>
> **2026-09-04 更新**: 重测脚本工具链已 ship (`phase-7a-baseline-retest-v2` change): `scripts/measure-baseline.py` + `scripts/evidence-gate-v1.sh` + `tools/baseline_schema_validate.py`。等待 §1 触发信号 (`docs/runbooks/baseline-retest.md`) 后即可执行真实 3 模型 baseline 重测。
> **Verdict scope (X 路线 per design D-3/D-4)**: parse_valid 单维度决议 + 数据完整性 check；task_success L1/L2/L3 仅签名占位（完整 D-4 "全部满足"语义由 ADR-0074 §决策 D5 v2 amendment 实施）

---

## §决议 (Verdict)

**Verdict**: **Conditional** (template + 函数 ship 决议,真实数据消费 OOS by MOMUS 修正)

**决议依据 (per `evaluate_gate()` 纯函数 + ADR-0074 §决策 D4 闭区间规则)**:

- 当前 ship 范围仅含 `evaluate_gate()` 决策树函数 + `test_evidence_gate` 7 cases + 决议文档模板；真实 LLM 决议数据消费 OOS（per `2026-08-25-evidence-gate-momus-decision-summary.md` X 选项 scope 锁定）
- baseline 数据（mock_mode = true）按 D-3 数据完整性 check 视为 **不构成真实 LLM 能力结论**；如严格按 D-3 range check 走 `parse_valid < 0 || > 1` 规则，mock_mode 数据会触发 **Conditional**（88.24% ∈ [85.0, 90.0)）
- 真实 3 模型 baseline 决议推迟至 Sprint 25+（独立 OpenSpec change 实施 ADR-0074 §决策 D5 v2 amendment）

**§决议字段**:

| 字段 | 值 | 来源 |
|------|-----|------|
| baseline_id | `2026-08-18-V3` | [execution-baseline-v3.yaml:1](2026-08-18-execution-baseline-v3.yaml) |
| parse_valid | **0.8824** | [execution-baseline-v1.md:42](2026-08-18-execution-baseline-v1.md) |
| task_success L1 | 0.8500 | [execution-baseline-v1.md:43](2026-08-18-execution-baseline-v1.md) |
| task_success L2 | 0.6000 | [execution-baseline-v1.md:44](2026-08-18-execution-baseline-v1.md) |
| task_success L3 | 0.1818 | [execution-baseline-v1.md:45](2026-08-18-execution-baseline-v1.md) |
| mock_mode | **true** | [execution-baseline-v1.yaml:16](2026-08-18-execution-baseline-v1.yaml) |
| prompt_version | V3 | [execution-baseline-v3.yaml:1](2026-08-18-execution-baseline-v3.yaml) |
| golden_tasks_total | 51 | [execution-baseline-v1.md:30](2026-08-18-execution-baseline-v1.md) |
| run_command | `tools/baseline/measure_prompt_baseline.py` | [ADR-0074 §决策 D3:1](../adr/adr-0074-prompt-evidence-gate.md) |
| holdout_verify | `scripts/verify_golden_holdout.sh` exit 0 | per T21 ship gate |
| timestamp | 2026-09-02T04:50:00Z | 本决议文档创建时间 |
| sha256 (yaml) | 见 `docs/audits/2026-08-18-execution-baseline-v3.yaml.sha256` (如需生成见 `sha256sum docs/audits/2026-08-18-execution-baseline-v3.yaml`) | per design Risks §决议不可复现 mitigation |

**Conditional 触发条件 (per ADR-0072 §决策 D2/D3 conditional triggers)**:
- 决议正式生效需 Sprint 25+ 真实 3 模型 baseline (无 mock_mode) 重测
- 真实 baseline 若仍 85% ≤ parse_valid < 90% → 触发 ADR-0072 D3 declarative style 实施（C6, 4h P0*）→ execution-dsl change
- 真实 baseline 若 < 85% → 触发 ADR-0072 D2 `$var` 实施（C5, 8h P0*）→ execution-dsl change

---

## §数据 plan (Data Plan)

| Source | path:line evidence |
|--------|-------------------|
| baseline 报告 | [execution-baseline-v1.md](2026-08-18-execution-baseline-v1.md) (51 tasks, L1=20 / L2=20 / L3=11) |
| baseline yaml | [execution-baseline-v1.yaml](2026-08-18-execution-baseline-v1.yaml) (mock_mode: true, file:16) |
| baseline v3 yaml | [execution-baseline-v3.yaml](2026-08-18-execution-baseline-v3.yaml) (V3 prompt) |
| golden suite | `lib/prompts/golden/*.json` (51 tasks) |
| few-shot examples | `lib/prompts/few_shots/*.md` (32 examples, 4 维度 × 8) |
| prompt builders | `src/common/prompts/v{1,2,3}.cpp` (V1/V2/V3) |
| 决策树函数 | [`src/common/prompts/evidence_gate.h`](../../src/common/prompts/evidence_gate.h) (header-only, no IO) |

---

## §测量方法 (Measurement Method)

```yaml
baseline_id: 2026-08-18-V3
prompt_version: V3
models:
  - gpt-4-turbo       # real baseline PENDING (X 路线 Sprint 25+)
  - claude-3-5-sonnet # real baseline PENDING
  - deepseek-v2       # real baseline PENDING
golden_tasks_total: 51
parse_valid_rate: 0.8824     # mock_mode baseline (3 模型平均, 占位符)
task_success_rate:
  L1: 0.8500               # mock_mode baseline
  L2: 0.6000               # mock_mode baseline
  L3: 0.1818               # mock_mode baseline
mock_mode: true            # ⚠️ 真实决议需 false (X 路线 ship 后 独立 change 测量)
timestamp: 2026-09-02T04:50:00Z
run_command: python3 tools/baseline/measure_prompt_baseline.py --prompt V3 --golden-dir lib/prompts/golden/ --output docs/audits/<date>-execution-baseline-v3.yaml
holdout_verify: scripts/verify_golden_holdout.sh exit 0 (per T21 ship gate)
```

**测量方法说明 (per design §测量方法 + design Risks/Trade-offs)**:
- parse_valid = LLM 输出的合法 YAML/DSL 解析成功率（不含格式错误）
- task_success = 执行结果与 golden suite expected_output 的语义匹配率（按难度分 L1/L2/L3 三层）
- 数据完整性 check (per design D-3): baseline run_id + 3 模型全部报告 + YAML 字段无缺漏 + mock_mode=false 任一缺失 → 立即返回 `GateStatus::Abort`

---

## §决策树 (Decision Tree)

**`evaluate_gate()` 函数实现** ([`src/common/prompts/evidence_gate.h:26`](../../src/common/prompts/evidence_gate.h)):

```
D-3 数据完整性 check:
  if parse_valid < 0.0 || parse_valid > 1.0 → return Abort

D-4 左闭右开临界带 (per ADR-0074 §决策 D4):
  if parse_valid < 85.0 / 100.0 → return Fail        (触发 ADR-0072 D2 `$var` C5)
  if parse_valid < 90.0 / 100.0 → return Conditional (触发 ADR-0072 D3 declarative C6)
  return Pass                                          (跳过 C5/C6)
```

**4 状态枚举** ([`src/common/prompts/evidence_gate.h:10`](../../src/common/prompts/evidence_gate.h)):

| Status | 触发条件 | 后续动作 |
|--------|----------|----------|
| `Pass` | parse_valid ≥ 90.0% | C5/C6 跳过, Phase 6c 收官 |
| `Conditional` | 85.0% ≤ parse_valid < 90.0% | 触发 ADR-0072 D3 declarative style (C6, 4h P0*) |
| `Fail` | parse_valid < 85.0% | 触发 ADR-0072 D2 `$var` (C5, 8h P0*) |
| `Abort` | 数据缺失或 incomplete | 不裁决, 要求 C1+C2+C3 重新测量 |

**5 边界单元测试** ([`tests/test_evidence_gate.cpp`](../../tests/test_evidence_gate.cpp)):
- parse_valid=84.9% → Fail（临界带下方）
- parse_valid=85.0% → Conditional（左闭入口）
- parse_valid=89.9% → Conditional（临界带内）
- parse_valid=90.0% → Pass（右开出口）
- parse_valid=sentinel(-1.0)/1.5 → Abort（数据完整性）
- parse_valid=0.8824% → Conditional（mock baseline 演示）
- to_string 映射 4 状态字符串

---

## §行动项 (Action Items)

| # | 行动 | 触发条件 | 当前状态 |
|---|------|----------|----------|
| 1 | **C4 Evidence Gate ship + archive** (本 change) | per ADR-0074 §决策 D4 Wave 2 准出 | ✅ ship (2026-09-02) |
| 2 | **真实 3 模型 baseline 重测** (X 路线 ship 后独立 change) | mock_mode=false baseline 测量完成 | ⏳ Sprint 25+ PENDING |
| 3 | **ADR-0074 §决策 D5 v2 amendment** (task_success L1/L2/L3 完整判定) | 真实 baseline 数据消费就绪 | ⏳ Sprint 25+ PENDING |
| 4 | **Wave 3 启动评估** (`from-roadmap-phase-6c-execution-dsl`) | Evidence Gate PASS（真实数据，非 mock） | ⏳ Phase 7 启动条件 5/6 待满足 |
| 5 | **Phase 7 启动条件项 #1 翻牌** (active-status.md §四) | 本决议 PASS | ⏳ Conditional (X 路线 placeholder 决议) |

**Conditional 决议路径说明 (per design D-4 + MOMUS REJECT 修正)**:
- 当前决议为 **Conditional** 因 baseline 为 mock_mode（不构成真实 LLM 能力结论）
- Conditional 不触发 C5/C6 立即实施（per proposal Impact §MUST NOT: 决议文档仅记录建议，C5/C6 启动需独立 OpenSpec change + 人类评审）
- 真实决议需 mock_mode=false baseline 测量完成（X 路线 ship 后由独立 OpenSpec change 实施）
- v1 ship 范围限于 `evaluate_gate()` 函数 + `test_evidence_gate` 7 cases + 决议文档模板 + ADR-0074 §决策 D5 实证字段追加 + active-status.md 同步

---

## §决议 (Verdict - Summary)

**Single-choice decision** (per spec Requirement "Active-Status Sync Within 24h" Scenario):

**Conditional** — 决议依据: mock baseline 88.24% 在临界带 [85.0, 90.0); 但 mock_mode=true 不构成真实 LLM 能力结论;真实数据消费 scope 待 Sprint 25+ 独立 OpenSpec change 实施 (X 路线 ship 后)。

**决议后 24h 内同步** (per spec Requirement + design §MUST DO 4):
- `docs/active-status.md` §一 Phase 6c 状态行：追加 "Evidence Gate v1 ship (commit XXX, 模板+函数决议 + mock baseline 88.24% Conditional placeholder) — 真实决议推迟 Sprint 25+"
- `docs/active-status.md` §四 Phase 7 启动条件项 #1 状态：Conditional (🟡)
- `ADR-0074 §决策 D5 实证字段`：追加 2026-09-02 决议记录（file:line 引用本决议文档 + baseline v3 yaml）

**Out of Scope** (per proposal §Out of Scope + design §Non-Goals):
- ADR-0072 D2/D3 代码实施（C5/C6 by `from-roadmap-phase-6c-execution-dsl`）
- 持续测量基础设施（regression cron / 24h prompt 变更测量, ADR-0074 §决策 D3 末项, Sprint 28+）
- 阈值调整（85%/70% 等, ADR-0074 §不变量 4 强制架构组评审）

---

**决议责任人**: Solo Dev (Single-Developer Mode)
**决议日期**: 2026-09-02
**决议有效性**: Phase 6c Wave 2 → Wave 3 推进的唯一客观标准
**决议可审计性**: 所有数值引用具体 file:line 证据（如 [execution-baseline-v1.md:42](2026-08-18-execution-baseline-v1.md)）

**决议不可逆性** (per design Risks): 本决议不引入运行时依赖（`evaluate_gate()` 函数仅用于离线决议）；如发现错误可重新评估（追加 v2 决议，不覆盖 v1）。

---

## 关联证据链 (Audit Trail)

- **OpenSpec change**: `openspec/changes/from-roadmap-phase-6c-evidence-gate/` (proposal.md + design.md + spec.md + tasks.md)
- **依赖 baseline change**: `openspec/changes/from-roadmap-phase-6c-execution-baseline/` ✅ ship + archived 2026-08-18
- **依赖 schema change**: `openspec/changes/from-roadmap-phase-6c-schema-complete/` ✅ ship + archived 2026-08-18 (ADR-0073 D3)
- **决策树源码**: `src/common/prompts/evidence_gate.h` (62 行, header-only, no IO)
- **决策树测试**: `tests/test_evidence_gate.cpp` (7 TEST_CASEs, registered in CMakeLists.txt line 121)
- **ADR**: `docs/ADR/adr-0074-prompt-evidence-gate.md` §决策 D4/D5
- **MOMUS 修正**: `docs/audits/2026-08-25-evidence-gate-momus-decision-summary.md` (X 路线 scope 锁定)
- **决议文档模板**: `docs/audits/evidence-gate-v1.md.template` (本决议文档骨架来源)
- **后置 unlock**: `from-roadmap-phase-6c-execution-dsl` (Wave 3, 依赖本决议 PASS)