## 1. OpenSpec Artifacts

- [x] 1.1 创建 `openspec/changes/phase-7a-baseline-retest-v2/` 目录
- [x] 1.2 创建 `proposal.md` (含 Oracle B1/B2 修订)
- [x] 1.3 创建 `design.md` (含 Decision 6/7/8 新增 per Metis A7/A3/A4)
- [x] 1.4 创建 `specs/phase-7a-baseline-retest/spec.md` (含 7 Requirements, 18 Scenarios)
- [x] 1.5 创建 `tasks.md`（本文件）
- [ ] 1.6 创建 `docs/superpowers/plans/2026-09-04-phase-7a-baseline-retest-v2.md` 实施计划 (评审通过后)

## 2. 准备环境 (CI 依赖契约, per Decision 3 + Metis V3)

- [x] 2.1 创建 `requirements.txt` 含 `jsonschema>=4.18,<5.0`
- [x] 2.2 创建 `docs/baselines/` 目录 (mkdir -p, per Oracle B1 前置)
- [ ] 2.3 更新 `.github/workflows/ci.yml` 添加 `pip install -r requirements.txt` + `apt-get install -y jq` step (per Metis V3)
- [ ] 2.4 验证前置条件: `python3 -c 'import jsonschema'` exit 0 + `which jq` exit 0

## 3. TDD RED — 写失败测试

- [x] 3.1 创建 `scripts/fixtures/baseline_responses.json` (50 task × 3 model 确定性 Mock 响应, per Metis H6 独立 fixtures)
- [x] 3.2 创建 `tests/test_baseline_retest_scripts.cpp` (~150 行, 5 cases per Decision 4):
  - 案例 A: PASS 决议 (mock 95% parse_valid + 80% L1/L2/L3)
  - 案例 B: Conditional 决议 (mock 88% parse_valid + 75% L1)
  - 案例 C: FAIL parse_valid<85 (mock 80% parse_valid)
  - 案例 D: FAIL L1<70 (mock 95% parse_valid + 60% L1)
  - 案例 E: FAIL L2<50 (mock 92% parse_valid + 80% L1 + 40% L2) [per Oracle B2 判定表空洞修复]
- [x] 3.3 注册到 `tests/CMakeLists.txt` (Catch2, 与既有 test_basic.cpp 模式一致)
- [ ] 3.4 验证测试因脚本缺失而失败

## 4. TDD GREEN — 最小实现

- [x] 4.1 新增 `scripts/measure-baseline.py` (~250 行):
  - argparse (--models / --mode / --prompts / --dimensions / --tasks-dir / --output / --concurrency / --check-env)
  - `--tasks-dir lib/prompt/golden/` 加载 54 个既有 JSON (per Oracle B1 修复)
  - concurrent.futures.ThreadPoolExecutor 并发调用 (per Decision 7, default 3)
  - mock mode 使用 `scripts/fixtures/baseline_responses.json` (per Metis H6 独立 fixtures)
  - real mode 输出 stub + NotImplemented (per Metis F4, 仅 OpenAI-compatible 接口预留)
  - JSON 输出 schema 与 `tools/baseline/measure_prompt_baseline.py` 兼容 (per Decision 8)
- [x] 4.2 新增 `scripts/evidence-gate-v1.sh` (~80 行, bash + jq):
  - shebang `#!/usr/bin/env bash` (per Decision 2)
  - 接受 baseline JSON 路径参数 + 可选 `--write-markdown <path>` 选项
  - 完整 4 维判定表 (per Oracle B2 修复): PASS / Conditional / FAIL (3 类触发条件)
  - parse_valid × 100 转 int 比较避免 jq 浮点尾数 (per Metis F1)
  - 输出 JSON (stdout) + Markdown (stdout) 双格式
  - `--write-markdown` 自动写文件供 `control-plane-eval.py` C5 消费
  - exit 0 (PASS/Conditional/Mock) / exit 1 (FAIL/JSON invalid)
- [x] 4.3 新增 `tools/baseline_schema_validate.py` (~100 行, Python + jsonschema):
  - 接受 baseline JSON 路径
  - 使用 `jsonschema.Draft202012Validator` (per Decision 3)
  - 校验必需字段 + 类型
  - 输出 diagnostic (exit 0 / 1)
- [ ] 4.4 验证 5 场景测试 PASS

## 5. 文档同步

- [x] 5.1 更新 `docs/runbooks/baseline-retest.md` §2 脚本路径引用 (新增 3 个脚本) + §2 命令 flag 修正 `--tasks docs/baselines/golden-suite-50.yaml` → `--tasks-dir lib/prompt/golden/` + §3 修复 `python3 scripts/evidence-gate-v1.sh` → `bash scripts/evidence-gate-v1.sh` (per Metis A2 pre-existing bug + Oracle 二审 flag 修正)
- [x] 5.2 更新 `docs/audits/2026-09-02-evidence-gate-v1.md` 标注 "等待真实 baseline 重测 (per runbook §1)"
- [x] 5.3 更新 `docs/active-status.md` §Sprint 25+ carry-over Route B closed

## 6. 验证 + Archive + Commit

- [ ] 6.1 Catch2 测试全 PASS (`ctest --test-dir build -R test_baseline_retest_scripts`)
- [ ] 6.2 `tools/adr_lint.py` 零错误
- [ ] 6.3 `openspec validate --strict` PASS
- [ ] 6.4 Oracle ac-verifier 验收
- [ ] 6.5 archive change + iteration.json +1 entry
- [ ] 6.6 git commit (1 atomic commit)
- [ ] 6.7 push to origin/main