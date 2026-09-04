## ADDED Requirements

### Requirement: scripts/measure-baseline.py 测量入口 (修正 B1: 复用既有 lib/prompt/golden/*.json)

The system SHALL provide `scripts/measure-baseline.py` as a CLI tool for real 3-model baseline measurement per `docs/runbooks/baseline-retest.md` §2. The tool MUST accept `--models` (CSV), `--mode` (real | mock), `--prompts` (CSV), `--dimensions` (CSV), `--tasks-dir` (path to `lib/prompt/golden/` JSON directory), `--output` (path to JSON output), `--concurrency` (parallel model count, default 3).

#### Scenario: 全量测量 3 模型 × 4 维度 × 54 tasks (修正 B1)
- **WHEN** operator runs `python3 scripts/measure-baseline.py --models claude-opus-4.5,kimi-k2.6,gpt-5 --mode real --prompts v1_schema,v2_fewshot,v3_two_phase --dimensions structured,tool_call,error_recovery,long_context --tasks-dir lib/prompt/golden/ --output docs/baselines/baseline-real-<date>.json`
- **THEN** system MUST load 54 existing JSON tasks from `lib/prompt/golden/`, generate 1944 samples (3 models × 3 prompts × 4 dims × 54 tasks) using parallel model calls (concurrency=3 default), output JSON with parse-valid + task-success L1/L2/L3 per sample

#### Scenario: 环境检查模式
- **WHEN** operator runs `python3 scripts/measure-baseline.py --check-env --models <list> --mode real`
- **THEN** system MUST verify each model API reachable + report status, exit 0 if all reachable / exit 1 with diagnostic

#### Scenario: Mock 模式 (CI / 测试用, fixture 来自独立 fixtures 目录)
- **WHEN** operator runs with `--mode mock`
- **THEN** system MUST use deterministic mock responses from `scripts/fixtures/baseline_responses.json` (per Metis H6: 独立 fixtures, 不反向依赖测试), zero external API calls, exit 0

#### Scenario: 并发模型调用 (修正 A3)
- **WHEN** operator runs without `--concurrency` flag
- **THEN** system MUST default to 3 concurrent model calls (one per model, per spec §2 1944 样本 ≤6h 预算约束)

### Requirement: scripts/evidence-gate-v1.sh 决议脚本 (修正 B2: 完整判定表)

The system SHALL provide `scripts/evidence-gate-v1.sh` as a CLI tool with shebang `#!/usr/bin/env bash` (per Metis A2 统一) for Evidence Gate re-evaluation per `src/common/prompts/evidence_gate.h` + runbook §3 thresholds. **完整判定表** (per Oracle B2 修复):

| 决议 | 触发条件 |
|------|----------|
| **PASS** | parse_valid ≥90% AND L1 ≥70% AND L2 ≥50% AND L3 ≥30% (3-model avg) |
| **Conditional** | parse_valid ∈ [85%, 90%) AND L1 ≥70% (L2/L3 仅记录) |
| **FAIL** | parse_valid <85% OR L1 <70% OR (parse_valid ≥90% AND (L2 <50% OR L3 <30%)) |

**Shebang 统一 (per Metis A2 + runbook bug)**: 第一行 `#!/usr/bin/env bash`, 调用约定统一 `bash scripts/evidence-gate-v1.sh <baseline.json>`, 不使用 `python3 scripts/evidence-gate-v1.sh`.

#### Scenario: PASS 决议 (完整 4 维度)
- **WHEN** operator runs `bash scripts/evidence-gate-v1.sh docs/baselines/baseline-real-<date>.json` AND input JSON shows parse_valid ≥90% AND L1 ≥70% AND L2 ≥50% AND L3 ≥30% (3-model avg)
- **THEN** system MUST output `PASS` + JSON `{verdict: "PASS", parse_valid, l1, l2, l3, baseline_id}` + Markdown summary

#### Scenario: Conditional 决议 (临界带)
- **WHEN** input JSON shows parse_valid ∈ [85%, 90%) AND L1 ≥70%
- **THEN** system MUST output `Conditional` + JSON + Markdown (记录待重测计划)

#### Scenario: FAIL 决议 (3 类触发条件全覆盖, per B2 修复)
- **WHEN** input JSON shows parse_valid <85% OR L1 <70% OR (parse_valid ≥90% AND (L2 <50% OR L3 <30%))
- **THEN** system MUST output `FAIL` + JSON + Markdown (记录失败原因 + ADR-0072 D2 触发可能)

#### Scenario: Partial Models 降级 (runbook §4 #4, per Metis H4)
- **WHEN** input JSON has `models_partial: ["X"]` field (1/3 models unavailable)
- **THEN** system MUST output `Conditional` with verdict_json annotation `models_partial: ["X"]`, exit 0

#### Scenario: Mock mode (per H1)
- **WHEN** input JSON has `mock_mode: true`
- **THEN** system MUST output verdict=Conditional with annotation `mock_baseline: true`, exit 0 (per Sprint 25 Conditional 决议占位语义)

### Requirement: tools/baseline_schema_validate.py JSON 校验 (修正 A4 + Metis F2)

The system SHALL provide `tools/baseline_schema_validate.py` as a CLI tool for validating baseline JSON output against JSON Schema 2020-12 (per ADR-0073 C++ validator 对齐, 修正 Metis F2 "Draft 7 vs 2020-12" 冲突). Uses jsonschema library Draft202012Validator.

**JSON Schema 兼容性 (per Metis V1)**: baseline.json 顶层字段集必须与 `tools/baseline/measure_prompt_baseline.py` baseline.json 兼容 (`baseline_id` / `models` / `mock_mode` / `generated_at` + 新增 `models.<model>.task_success.{L1,L2,L3}` 子字段).

#### Scenario: Schema 校验通过
- **WHEN** operator runs `python3 tools/baseline_schema_validate.py docs/baselines/baseline-real-<date>.json` AND file is valid JSON conforming to schema
- **THEN** system MUST exit 0 with "✓ valid" message

#### Scenario: Schema 校验失败
- **WHEN** file is missing required fields OR has wrong types
- **THEN** system MUST exit 1 with diagnostic listing specific field violations

### Requirement: Mock 模型单元测试 (用 Catch2 + spawn 子进程, 修正 Metis A1)

The system SHALL provide `tests/test_baseline_retest_scripts.cpp` with 5 cases covering: PASS / Conditional / FAIL(parse_valid<85) / FAIL(L1<70) / FAIL(parse_valid≥90 AND L2<50) verdict scenarios.

**测试框架决策 (per Metis A1)**: 用 Catch2 + `system()` / `popen()` spawn Python 脚本 (项目无 pytest 历史), 与既有 `test_basic.cpp` / `test_executor.cpp` 模式一致.

#### Scenario: PASS 场景测试
- **WHEN** test runs `python3 scripts/measure-baseline.py --mode mock --output /tmp/b.json` (with fixtures giving 95% parse_valid + 80% L1/L2/L3) THEN `bash scripts/evidence-gate-v1.sh /tmp/b.json`
- **THEN** verdict MUST be `PASS`

#### Scenario: FAIL L2 场景测试 (修正 B2 判定表空洞)
- **WHEN** test runs with fixtures giving 92% parse_valid + 80% L1 + 40% L2 + 35% L3
- **THEN** verdict MUST be `FAIL` (per B2 新增 L2 判定路径)

### Requirement: CI 依赖契约 (per Metis H2 + V3)

The system SHALL provide `requirements.txt` + GitHub Actions jq install step to guarantee jsonschema + jq availability.

#### Scenario: requirements.txt 包含 jsonschema
- **WHEN** CI runner builds the project
- **THEN** `python3 -c 'import jsonschema'` MUST exit 0 AND `jsonschema.__version__` MUST be ≥4.18

#### Scenario: jq 可用性
- **WHEN** CI runner runs `which jq && jq --version`
- **THEN** output MUST show non-empty path + version ≥1.6

### Requirement: 文档同步

The system SHALL update documentation per Oracle B2 引用源修正 + Metis A6 C5 接口契约.

#### Scenario: runbook 脚本路径引用更新 + runbook §3 调用约定修正
- **WHEN** operator opens `docs/runbooks/baseline-retest.md` §2 + §3
- **THEN** each command references actual shipped script path AND §3 修复 `python3 scripts/evidence-gate-v1.sh` → `bash scripts/evidence-gate-v1.sh`

#### Scenario: control-plane-eval.py C5 自动消费
- **WHEN** `bash scripts/evidence-gate-v1.sh docs/baselines/baseline-real-<date>.json --write-markdown docs/audits/<date>-evidence-gate-v1.md` runs with verdict=PASS
- **THEN** `docs/audits/<date>-evidence-gate-v1.md` MUST contain "Verdict: PASS" line that `scripts/control-plane-eval.py` can grep

#### Scenario: active-status.md Route B closed
- **WHEN** this change is archived
- **THEN** `docs/active-status.md` §Sprint 25+ carry-over marks Route B as `✅ closed 2026-09-04 (scripts toolchain ship, waiting for trigger signal)`

### Requirement: 决策声明 (修正 B2 阈值引用源 + Metis ⚠️)

The system SHALL document that `evidence-gate-v1.sh` 实施 Evidence Gate v1 多维度判定逻辑 (parse_valid + L1/L2/L3 完整表), 而非 `src/common/prompts/evidence_gate.h` 的 parse_valid-only 占位实现. 两者并存, bash 脚本作为 Phase 7a 重测专用入口.

**ADR 状态说明 (per Metis ⚠️)**: 本 change **不**隐式触发 ADR-0074 §决策 D5 v2 amendment. spec 完整 L1/L2/L3 判定仅在 `evidence-gate-v1.sh` bash 脚本中实施, 不修改 `src/common/prompts/evidence_gate.h` C++ 实现 (后者维持 v1 parse_valid-only 占位, 待 Sprint 25+ 独立 change 处理).

#### Scenario: 两实现并存不冲突
- **WHEN** operator queries ADR-0074 §决策 D5 status via `tools/adr_lint.py`
- **THEN** ADR-0074 status remains unchanged (no implicit amendment via this change)