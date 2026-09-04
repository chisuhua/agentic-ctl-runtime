## Context

Evidence Gate v1 (per `src/common/prompts/evidence_gate.h`) 决议 = **Conditional** (mock 88.24% ∈ [85, 90) 临界带), 不是真实 LLM 能力结论. Phase 7a 启动条件 C5 因此处于 🔒 阻塞态.

`docs/runbooks/baseline-retest.md` (Sprint 25 Change #5 ship, commit `f1a6397`) 已定义 4 部分契约:
- §1 触发信号: `docs/audits/<date>-baseline-window-open.md` 文件存在 + 修改时间 ≤7 天
- §2 重测 Runbook: 3 模型 × 4 维度 × 50 tasks = 648 样本, ≤6h 完成
- §3 Evidence Gate 重跑: PASS ≥90% / Conditional ∈[85,90) / FAIL <85%
- §4 容量预算 + 失败 Fallback: ≤8h 总预算, 5 种失败模式有降级路径

Phase 6c `from-roadmap-phase-6c-execution-baseline` 已 ship 但 `scripts/measure-baseline.py` + `scripts/evidence-gate-v1.sh` + `tools/baseline_schema_validate.py` 三个脚本**未落地** (仅定义在 runbook 中). 本 change 落地这三个脚本作为可执行工具链, 让 §1 触发信号出现时可一键执行.

**关键事实 (per Oracle + Metis 现场验证)**:
- `docs/baselines/golden-suite-50.yaml` **不存在** — 真实 golden 数据在 `lib/prompt/golden/*.json` (54 tasks)
- `src/common/prompts/evidence_gate.h` shipped v1 仅基于 parse_valid (L1/L2/L3 pass-through)
- `tools/baseline/measure_prompt_baseline.py` 已 ship (Phase 6c T21, Mock mode)
- 项目无 pytest 历史, 全部 Catch2/CMake

## Goals / Non-Goals

**Goals**:
1. 落地 `scripts/measure-baseline.py` (3 模型 × 4 维度 × 54 tasks 测量入口, JSON 输出, 并发调用)
2. 落地 `scripts/evidence-gate-v1.sh` (per `evidence_gate.h` + runbook §3 完整 4 维判定 PASS/Conditional/FAIL)
3. 落地 `tools/baseline_schema_validate.py` (JSON Schema 2020-12 校验)
4. 落地 Catch2 测试 `tests/test_baseline_retest_scripts.cpp` (5 cases, 用 system()/popen spawn Python 脚本)
5. CI 依赖契约: `requirements.txt` (jsonschema) + GitHub Actions jq install step
6. 文档同步: `docs/runbooks/baseline-retest.md` §3 修复 pre-existing bug + scripts 路径引用 + active-status.md
7. `--write-markdown` 选项: 自动生成 `docs/audits/<date>-evidence-gate-v1.md` 让 `control-plane-eval.py` C5 可自动消费
8. **不**实际执行真实 baseline 重测 (依赖外部模型窗口, 不在本 change scope)
9. **不**修改 `src/common/prompts/evidence_gate.h` C++ v1 实现 (维持 parse_valid-only 占位, 待 Sprint 25+ 独立 change 处理 D5 v2 amendment)

**Non-Goals**:
1. **不**隐式触发 ADR-0074 §决策 D5 v2 amendment (per Metis ⚠️ — 本 change 仅在 bash 脚本中实施完整判定, 不修改 C++ 实现)
2. **不**创建 `docs/baselines/golden-suite-50.yaml` (per Oracle B1 — 复用既有 54 个 lib/prompt/golden/*.json`)
3. **不**创建 `baseline-window-open.md` 触发信号文件 (依赖 Solo Dev 容量判定)
4. **不**改 mock baseline 工具链 (`scripts/measure_prompt_baseline` 已 ship)
5. **不**实际调用 3 模型 API (本 change 仅落脚本骨架 + Mock 模式 + interface, 真实测量待 §1 触发)

## Decisions

### Decision 1: 3 个独立脚本而非 monolith (保留)

- **选择**: 拆分 `measure-baseline.py` (测量) + `evidence-gate-v1.sh` (决议) + `baseline_schema_validate.py` (校验) 为独立可执行
- **理由**: runbook §4 失败模式隔离 + Unix 工具链哲学 + 可独立测试
- **替代考虑**: 单 mega 脚本 ❌ (失败无法定位阶段)

### Decision 2: Python + bash 混合 (修订 per Metis F1/F2)

- **选择**: `measure-baseline.py` + `baseline_schema_validate.py` 用 Python 3.11+, `evidence-gate-v1.sh` 用 bash + jq
- **理由**: Python 是项目首选 (per `tools/adr_lint.py` 等), bash 适合简单决议逻辑
- **shebang 统一 (per Metis A2)**: `evidence-gate-v1.sh` 第一行 `#!/usr/bin/env bash`, 调用约定统一 `bash scripts/evidence-gate-v1.sh` (不使用 `python3 scripts/evidence-gate-v1.sh` 修 pre-existing runbook bug)
- **jq 数字比较安全 (per Metis F1)**: parse_valid 乘 100 转 int 比较, 避免浮点尾数敏感

### Decision 3: jsonschema Draft202012Validator (修订 per Metis F2)

- **`baseline_schema_validate.py` 使用 `jsonschema.Draft202012Validator`** (而非默认 Draft 7), 与项目既有 C++ `nlohmann/json_schema_validator` (ADR-0073) 对齐
- **新增依赖**: `jsonschema>=4.18,<5.0` (项目首次 Python 三方依赖, 通过 `requirements.txt` + CI 安装保障)

### Decision 4: Catch2 + spawn Python 脚本测试 (修订 per Metis A1)

- **选择**: `tests/test_baseline_retest_scripts.cpp` 用 Catch2 + `system()` / `popen()` spawn Python 脚本
- **理由**: 项目无 pytest 历史 (`tests/test_control_plane_eval.py` 是 C++ Catch2 包装 Python), 与既有 `test_basic.cpp` 模式一致
- **禁止**: 不引入 pytest 体系 (scope 越界)

### Decision 5: 双格式输出 + --write-markdown 选项 (修订 per Metis A6)

- **选择**: `evidence-gate-v1.sh` 输出 JSON (stdout) + Markdown (stdout), 加 `--write-markdown <path>` 选项自动写文件
- **理由**: 
  - JSON 供 `scripts/control-plane-eval.py` 机器消费 (Sprint 25 Change #2)
  - Markdown 供 Solo Dev 人工审计
  - `--write-markdown` 自动生成 `docs/audits/<date>-evidence-gate-v1.md` 让 control-plane-eval.py C5 可 grep "Verdict: PASS" 自动转 PASS (无需手工编辑 markdown)

### Decision 6 (新增 per Metis A7): evidence-gate-v1.sh 与 evaluate_gate 关系

- **选择**: bash 脚本**重写**决策逻辑 (不调用 C++ `evaluate_gate()`)
- **理由**: 
  - bash + jq 是简单决议脚本, subprocess 调用 C++ binary 复杂且依赖 .so 加载
  - 两实现并存不冲突 (本 change scope 不动 C++ 实现)
  - 决议语义文档化 (spec.md 完整判定表), 与 C++ v1 单一事实源 (parse_valid-only) 明确区分
- **替代考虑**:
  - ❌ subprocess 调用 evaluate_gate — 复杂且过设计
  - ❌ ctypes 调用 .so — 依赖动态库加载
  - ✅ bash 重写 — 简单 + 文档化 + 与 runbook §3 对齐

### Decision 7 (新增 per Metis A3): 3 模型并行 vs 串行

- **选择**: `measure-baseline.py --concurrency N` 默认 3 (3 模型并行)
- **理由**: 
  - 648 样本串行 (3 模型每模型 ~2h) = 6h 正好卡 8h 预算, 容错空间极小
  - 3 模型并发 = 每个模型 ~2h, 单模型失败不阻塞其他
- **约束**: `concurrency <= len(models)`, max 10 (避免 API rate limit)

### Decision 8 (修订 per Oracle 二审): JSON Schema 兼容性

- **`baseline.json` 顶层字段集**对齐 `tools/baseline/measure_prompt_baseline.py` baseline.json 既有 schema (修正 Oracle 二审字段名错误):
  - **复用字段**: `baseline_id` / `llms` (注意非 `models`) / `mock_mode` / `generated_at` / `golden_tasks`
  - **类型升级**: per-model `task_success` 从 flat float 升级为 `task_success.{L1,L2,L3}` 嵌套对象 (per ADR-0074 D5 v2 semantics, schema v2)
- **理由**: 顶层字段名保持兼容 (避免 `control-plane-eval.py` 等下游消费者破坏), 仅 per-model `task_success` 类型升级 (从单维度 float → 多维度 L1/L2/L3)
- **下游影响**: C5 检测仅 grep markdown (`Verdict[:：]\s*\*?\*?PASS`), 不解析 baseline JSON, 故类型升级对 C5 检测零影响

## Risks / Trade-offs

| 风险 | 缓解 |
|------|------|
| bash + jq 跨平台 (macOS bash 3.2 缺功能) | 限定 Linux (项目目标平台 per CMake); 显式 shebang `#!/usr/bin/env bash` + README 标注 GNU bash ≥4.0 要求 |
| jsonschema 依赖 CI 必崩 | `requirements.txt` 列出 + CI step `pip install -r requirements.txt` + GitHub Actions jq install |
| bash + jq 数字比较浮点尾数 | parse_valid 乘 100 转 int 比较 (per Metis F1) |
| argparse subcommand 组合错误 | `--check-env` 与全量参数互斥组, `--mode mock` 不调真实 API |
| 3 模型 API 协议差异 (OpenAI/Anthropic/Google) | real mode 明确降级为 adapter stub (per Metis F4), 仅 OpenAI-compatible 端点支持 |
| ADR-0074 §决策 D5 v2 amendment 隐式触发 | Decision 6 明确不修改 C++ 实现, 仅 bash 脚本实施, ADR 状态不变 |
| `docs/baselines/` 目录不存在 | `mkdir -p docs/baselines/` 作为 tasks 3.x 前置 |
| `golden-suite-50.yaml` 不存在 (per Oracle B1) | 复用既有 54 个 lib/prompt/golden/*.json` (per Decision 8), 不创建 YAML |
| runbook §3 `python3 scripts/evidence-gate-v1.sh` 调用约定错误 | Decision 2 + tasks 4.1 修复 runbook, 统一 `bash scripts/evidence-gate-v1.sh` |

## Migration Plan

**无迁移** (纯增量):
1. 新增 3 个脚本 + 1 个测试 + `requirements.txt` + `scripts/fixtures/baseline_responses.json`
2. 文档同步仅追加/修正链接
3. 若真实测量触发 (§1 信号): 创建 trigger file → 跑 measure-baseline.py → 跑 baseline_schema_validate.py → 跑 evidence-gate-v1.sh --write-markdown → C5 自动 PASS

## Open Questions

**无开放问题**. 范围明确: 3 个脚本 + 1 个测试 + requirements.txt + jq install + 3 个文档更新. 实施周期 ~2h (TDD 5 步).