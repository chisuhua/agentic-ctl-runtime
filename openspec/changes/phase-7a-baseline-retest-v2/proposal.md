## Why

Evidence Gate v1 (per `src/common/prompts/evidence_gate.h`) 决议 = **Conditional** (mock 88.24% ∈ [85, 90) 临界带), 不是真实 LLM 能力结论. Phase 7a 启动条件 C5 因此处于 🔒 阻塞态.

Phase 7a 6 项启动条件复评 per `scripts/control-plane-eval.py`:
- ✅ C1 (AgentForge ≥2 agents) — Sprint 25 U4 ship 已 PASS
- ❌ C2 (Solo Dev ≥2 人 OR ≥80h/双周) — 外部约束, 不可解药
- ❌ C5 (Evidence Gate 真实 PASS) — **本 Route B 唯一可代码解锁项**
- ❌ 其他 3 项 — 详见 active-status.md

`docs/runbooks/baseline-retest.md` (Sprint 25 Change #5 ship, commit `f1a6397`) 已定义 4 部分契约: §1 触发信号 + §2 重测 Runbook + §3 Evidence Gate 重跑 + §4 容量预算 + 失败 Fallback.

**本 change 目标**: 落地 §2 重测 Runbook 的脚本工具链 + §3 Evidence Gate 重跑决议脚本, 让 §1 触发信号出现时可一键执行 (≤8h 中断预算内), 不阻塞等待外部模型窗口.

**Oracle + Metis 评审修订 (per Round 2)**: 
- B1: 复用既有 54 个 lib/prompt/golden/*.json` (而非创建 `docs/baselines/golden-suite-50.yaml`)
- B2: 完整 4 维判定表 (parse_valid + L1/L2/L3, 含 L2/L3 失败判定路径)
- Metis A1: Catch2 测试 (非 pytest) + Decision 4
- Metis A7 + Decision 6: bash 脚本重写 (不调用 C++ evaluate_gate, 两实现并存)

## What Changes

- **新增** `scripts/measure-baseline.py` — 全量测量入口 (3 模型 × 4 维度 × 54 tasks, 并发 N=3, JSON 输出)
- **新增** `scripts/evidence-gate-v1.sh` — Evidence Gate 决议脚本 (per Decision 6 bash 重写, 含 `--write-markdown` 选项)
- **新增** `tools/baseline_schema_validate.py` — JSON Schema 2020-12 校验 (per Decision 3 Draft202012Validator)
- **新增** `tests/test_baseline_retest_scripts.cpp` — Catch2 + spawn Python 测试 (5 cases per Decision 4)
- **新增** `scripts/fixtures/baseline_responses.json` — 确定性 Mock 响应 fixture (per Metis H6 独立 fixtures)
- **新增** `requirements.txt` — `jsonschema>=4.18,<5.0` (项目首次 Python 三方依赖)
- **新增** `docs/baselines/` 目录 (per Oracle B1 前置)
- **更新** `.github/workflows/ci.yml` — 新增 `pip install -r requirements.txt` + `apt-get install -y jq` step (per Metis V3)
- **更新** `docs/runbooks/baseline-retest.md` §2 脚本路径引用 + §3 修复 `python3 scripts/evidence-gate-v1.sh` → `bash scripts/evidence-gate-v1.sh` (per Metis A2 pre-existing bug fix)
- **更新** `docs/audits/2026-09-02-evidence-gate-v1.md` — 标注 "等待真实 baseline 重测 (per runbook §1)"
- **更新** `docs/active-status.md` — Route B closed

**Non-goals**:
- **不**实际执行真实 baseline 重测 (依赖外部模型窗口, 不在本 change scope)
- **不**修改 Evidence Gate 阈值 (per ADR-0074 D4 已 ship)
- **不**创建 `baseline-window-open.md` 触发信号文件 (依赖 Solo Dev 容量判定)
- **不**改 mock baseline 工具链 (`tools/baseline/measure_prompt_baseline.py` Phase 6c 已 ship, 不重复造轮)
- **不**实际调用 3 模型 API (本 change 仅落脚本骨架 + Mock 模式 + interface, 真实测量待 §1 触发)
- **不**修改 `src/common/prompts/evidence_gate.h` C++ v1 实现 (per Decision 6, 两实现并存)
- **不**隐式触发 ADR-0074 §决策 D5 v2 amendment (per Decision 6 + Metis ⚠️)
- **不**创建 `docs/baselines/golden-suite-50.yaml` (per Oracle B1, 复用 54 个既有 JSON)

## Capabilities

### New Capabilities

- `phase-7a-baseline-retest`: Sprint 25+ carry-over U 项 — 真实 3 模型 baseline 重测的脚本工具链落地. 决策 1-10 per design.md (3 个脚本拆分 + Catch2 测试 + requirements.txt + --write-markdown + evaluate_gate 关系). §3 Evidence Gate 决议 PASS + `--write-markdown` 自动更新 `docs/audits/<date>-evidence-gate-v1.md` → `scripts/control-plane-eval.py` C5 自动转 PASS (per Decision 5 + Metis A6).

### Modified Capabilities

_(无现有 spec 修改; 新增独立工具链, 不影响既有 mock baseline 流程与 evidence_gate.h C++ 实现)_

## Impact

- **代码**:
  - `scripts/measure-baseline.py` (新增, ~250 行, argparse + concurrent.futures + JSON 输出)
  - `scripts/evidence-gate-v1.sh` (新增, ~80 行, bash + jq + --write-markdown 选项)
  - `tools/baseline_schema_validate.py` (新增, ~100 行, jsonschema Draft202012Validator)
  - `scripts/fixtures/baseline_responses.json` (新增, ~50 行, 50 task × 3 model 固定响应)
  - `requirements.txt` (新增, jsonschema + 版本约束)
  - `tests/test_baseline_retest_scripts.cpp` (新增, ~150 行, 5 cases × Catch2 + system())
  - `docs/baselines/` (新增空目录, mkdir 前置)
- **CI**:
  - `.github/workflows/ci.yml` (+10 行, `pip install -r requirements.txt` + `apt-get install -y jq` step)
- **文档**:
  - `docs/runbooks/baseline-retest.md` §2 + §3 (脚本路径引用 + 调用约定修复)
  - `docs/audits/2026-09-02-evidence-gate-v1.md` (+1 行, 等待标注)
  - `docs/active-status.md` (§Sprint 25+ carry-over Route B closed)
  - `openspec/changes/archive/2026-09-04-phase-7a-baseline-retest-v2/` artifacts
- **依赖**:
  - 上游: `tools/baseline/measure_prompt_baseline.py` (Phase 6c mock, baseline.json schema 兼容)
  - 下游: `scripts/control-plane-eval.py --dry-run --relaxed` 在 §1 触发信号出现后可直接调用本脚本
- **风险**: 低-中 (3 个新脚本 + Catch2 测试 + 文档同步, 真实测量延迟触发独立)
- **估时**: 2h (TDD 5 步, 单人可完成)