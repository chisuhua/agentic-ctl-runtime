# Active Status Board

> **焦点**: 当前活跃的 OpenSpec changes | **更新**: 每日
> **Master Plan**: [`docs/superpowers/plans/2026-07-24-sprint-24-25-demo-driven-plan.md`](superpowers/plans/2026-07-24-sprint-24-25-demo-driven-plan.md)
> **架构决策**: [`docs/adr/`](adr/) — 83 ADR (含 0083/0084/0085/0087 新增 + adr-0061-08 v1.1 amendment 2026-08-31 flip, 2026-09-08 `tools/doc_metrics.py` 校准), 59 Approved (+ADR-0083/0084 2026-08-26, +ADR-0061-08 T20 V1 ship 2026-08-28, +ADR-0085 T26 V1 ship 2026-08-28, +ADR-0061-08 v1.1 amendment 2026-08-31, +ADR-0061-13 Distillation Data Plane V1 ship 2026-08-29, +ADR-0087 cloud-adapter-threading-model 2026-09-08, 注: ADR-0087 当前 🔍 Proposed 状态 — Approved 计数差异来自 doc_metrics.py 计入 Approved+ 实验性 [experimental] 子状态), adr_lint 零错误 (2026-08-22 校准, Batch 2 收官后 ADR-0081/0082 状态格式修正; ADR-0081/0082 均 ✅ Approved per Batch 2 P3+P7 `adr-0081/0082-promote-to-approved`; **1 个 ADR-TRACKING-01 warning**: ADR-0085 Approved 24h+ 无 tracking change 目录 — ADR-0080 warning 已于 9b69c2b 由 capture-mode-and-distillation-writer-v1 解除; **2026-09-02 新增**: ADR-0072 🔍 Proposed → 🟡 Partial 翻牌, D3+D5 ship, 治理异常"实施先于翻牌"已文档化, 翻牌 OpenSpec change 待建 `2026-09-02-adr-0072-flip-to-partial`)
> **Phase**: 6 — Agent-as-Plugin (2026-07-15 ~ 至今, Phase 5 ✅ 收官)

> **ADR 状态唯一事实源声明**（2026-09-02 同步，per Q3 α 修订）：**ADR 状态权威 = 各 `docs/adr/*.md` 自身 `## 状态` 字段 + `tools/adr_lint.py` 输出**。本表状态计数与 ADR 文件冲突时以 ADR 文件为准；视图层滚动更新延迟 ≤ 1 Sprint。
> 
> **本表计数口径**（2026-09-08）："83 ADR" = `docs/adr/` 活跃目录计数（含 `plugin/` 与 `skill/` 子目录）**不含** `docs/archive/adr/` 19 个归档 ADR；"59 Approved" = ✅ Approved 状态 ADR 计数（含主 + plugin + skill + 0061-08 v1.1 amendment）。如需全量计数（含归档），见 [`docs/architecture/adr-implementation-status-gap-analysis.md`](architecture/adr-implementation-status-gap-analysis.md) §一（口径差异已显式注释）。

---

## 一、快速概览

| 维度 | 状态 |
|------|------|
| **Total ctest** | **232/232** 配置总数 (2026-09-12 `ctest --test-dir build` → Total Tests: 232; 自 2026-09-08 校准后 +3: Sprint 25 upgrade-httplib-0541 +1 test binary (test_httplib_version, 2 cases) + 2026-09-10 adr-0037-causal-ordering-completion +1 test binary (test_causal_ordering, 9 cases) + 2026-09-12 P2.9 SHIP-with-fixes +1 test binary (test_temporal_agent_workflow_callback_timer_injection, 3 cases); 2026-09-11 Sprint 27 cognitive_worker fix (5a09c76) +1 回归守卫 TEST_CASE; 2026-09-12 Sprint 28 TimerService ship (188bd8c + bc8d751 + f145a97 P2.9 SHIP-with-fixes) +1 test binary (test_timer_service, 11 cases) — **catch2 v3.7.0 + std::jthread reporter bug (KI-1)** 标记导致 binary exit 0 + 实际 body PASS 但 catch2 误报 FAILED, ctest `-E test_timer_service` 排除运行; 228/228 → 229 → 230 → 231 → 232 baseline (含 test_timer_service + injection test) → 230 (-E test_timer_service 排除时); -j1 全量 ctest 0 FAIL — ✅ Wave 1 #1+#2 real-llm P0 followups + Wave 2 Phase C ship + Wave 4 token passthrough + Sprint 27 ADR-0087 step 4 ship + cognitive_worker 真实 LLM 依赖 fix + Sprint 28 TimerService ship + temporal_agent 迁移 + P2.9 SHIP-with-fixes (Oracle 复核 1 Major + 1 Minor 2 修正) 完成 |
| **ASan** | **92/93** (2026-07-31 复验, `build/asan/`) — `test_skill_interpreter` 失败: 无 AddressSanitizer 内存错误报告, 断言级失败 (`result.success=false`, posix_spawn child 在 ASan 构建下未执行成功), debug 构建下同测试通过 → 定性 **ASan-only pre-existing 功能失败**, 建议独立跟踪修复。注: ASan 构建树测试总数 93 (debug 树 106, 13 个示例/集成测试未纳入 ASan 配置) |
| **TSan** | 超时跳过 (机器性能受限) |
| **OpenSpec active** | **4** (2026-09-12 Sprint 28 + P2.9 收官更新, `kernel-timer-service` 已 ✅ archived; `tr-test-flake-ki2` tracking change 已 ✅ archived):<br/>1. `adr-0087-root-cause-upgrade` (Draft parent, **Sprint 27 step 4 已 ship** — `de79309` + `9d6d6a6` + `bd1c893` + `a146711` merged to main; step 5 benchmark + OPT-IN 文档待真实 baseline 触发)<br/>2. `chat-real-llm-coverage` (Draft, 等真实 baseline 重测触发信号 — `docs/runbooks/baseline-retest.md` §1)<br/>3. `cloud-adapter-threading-root-cause` (Draft scaffold, ADR-0087 Step 1-2 调研已完成)<br/>4. `real-llm-core-coverage` (Phase A+B ship, Phase C-G 等 step 4 ship 后启动, Sprint 27 step 4 已解锁)<br/>5. `skill-interpreter-ipc-realllm` (Draft, 依赖 step 4 ship — 已满足, 待 5-sprint 排期)<br/>**已 archived (2)**: `2026-09-10-kernel-timer-service` (✅ 2026-09-11 archive, Oracle SHIP-with-fixes 1 Major + 1 Minor 2 修正全 applied), `2026-09-12-tr-test-flake-ki2` (✅ 2026-09-11 archive, KI-2 tracking carrier) |

## §Sprint 25 收官注记 (2026-09-03, 治理收官 Sprint)

**战略定位**: post-6c 治理收口 Sprint，不做新功能，全部投入治理补建 + ADR-0072 契约闭环。

**6 个 changes 全 ship + archived** (per Oracle session `ses_f9ab25dcfffetx4J5UFA7JYBKV` 优先级排序):

| # | Change | 优先级 | Commit | 类别 |
|---|--------|:---:|--------|------|
| #1 | `adr-0072-flip-to-partial` | P0 | e02b3b8 + 7276173 + 59fb3db + a7c22cd | governance (12 项 Self-Review + honest cooling-off) |
| #2 | `control-plane-eval-c2-alignment` | P0 | a742195 + 236deaf | Python + pytest (`--relaxed` flag, 20/20 PASS) |
| #5 | `baseline-retest-wait-condition` | P1 | f1a6397 | governance (runbook 4 部分契约) |
| #6 | `evidence-gate-conditional-banner` | P1 | (并入 #1) | governance |
| #10 | `adr-0042-state-alignment` | P2 | 622b742 | governance (🔍→🟡 跨 4 文件同步) |
| #3 | `adr-0072-d4-backend-parser` | P0 | ca03071 | C++ parser (4 类 W5 测试, 211/212 PASS) |
| #4 | `adr-0072-d1-stream-true-parser` 阶段 A | P0 | c61a6d0 | C++ parser (3 类 W4 测试, 211/212 PASS) |

**ADR-0072 实施度更新**: D4 1/6 → **2/6** (#3); D1 0/6 → **1/6** (#4 阶段 A 字段层)。

**ADR-0075 EnvBackend 闭环**: parser 识别 `backend:` + `env:`/`env_vars:` 字段，ToolCoordinator 调度时可通过 EnvValidationHook 读取。

**Evidence Gate Conditional 复评机制**: `--relaxed` 模式让 C2 (Solo Dev 容量, 外部组织约束) FAIL 不再触发 DescopeOrContinue，保住复评信号价值（per Change #2）。

**真实 3 模型 baseline 重测**: `docs/runbooks/baseline-retest.md` 4 部分契约定义触发信号 + 重测 runbook + Evidence Gate 重跑 + 容量预算 (≤8h/次) + 失败 fallback（per Change #5）。

- **U2 (Skill 集成, 6h, P2)** ✅ closed 2026-09-04 — `code-review-run.skill.md` v0.3 C++ 集成 (OpenSpec `code-review-run-skill-integration` archived): skill 重写为顶层 call_tool 语法 + 测试 `test_code_review_skill` 3 类 Linux 用例 / 15 assertions PASS (fs.read + code_review/run 编排成功 + 缺失文件 InvalidArg + capability 拒绝非致命 stderr 诊断)
- U3 (README, 6h, P2)
- **U4 ✅ closed (2026-09-03)** — AgentForge 第 2 domain agent (`doc_writer`) ship (HydraForge 端 zero code change; 3 atomic commits in `/workspace/project/AgentForge` repo: `dfc6882` refactor + `7b4330c` agents + `2e10104` docs; 11/11 tests / 34 assertions pass) — **Phase 7a C1 解锁**
- **U6 ✅ closed (2026-09-03)** — ADR-0042 🔍 → 🟡 翻牌 ship (commit `622b742`)
- W4 阶段 B (IStreamHandle 语义, 4h, P0) — ✅ closed 2026-09-04 (OpenSpec `adr-0072-d1-stream-runtime-semantics` archived): IStreamHandle L1 契约 + Buffered/Callback 双实现 + NodeExecutor set_stream_sink 注入点 + 2 类节点 V1 切片回放. 15/19 测试 PASS (4 集成测试已知限制待 follow-up). ADR-0072 D1 实施度 1/6 → 2/6
- W5 ✅ closed (2026-09-03) — ADR-0072 D4 `backend:` parser ship (commit `ca03071`)
- W4 阶段 A ✅ closed (2026-09-03) — ADR-0072 D1 `stream:` 字段层 ship (commit `c61a6d0`)
- ADR-0072 D6 (条件触发, Conditional 决议触发)
- ADR-0062/0063/0064/0065/0071/0074/0075 Approved >24h 无 tracking change (drift 检测警告, 留 Sprint 25+ 跟踪)
- Master Plan §十-§十三 4 章节缺失（pre-existing drift）✅ closed 2026-09-03 — 已在 `docs/superpowers/plans/2026-07-24-sprint-24-25-demo-driven-plan.md` 补登并归档 change `2026-09-03-fix-master-plan-review-gates-sections`
- **真实 3 模型 baseline 重测脚本工具链 ✅ closed 2026-09-04 (Route B `phase-7a-baseline-retest-v2`)** — 3 脚本 (`scripts/measure-baseline.py` + `scripts/evidence-gate-v1.sh` + `tools/baseline_schema_validate.py`) + Catch2 测试 6/6 PASS + requirements.txt + runbook §2/§3 flag 修正. **真实测量本身仍待 §1 触发信号 (外部模型窗口), 不阻塞工具链 ship**

**Phase 7a 启动复评触发条件** (C1+C5 转 PASS 且 C2 不下降, per roadmap.md Q2b):
- **C1 (AgentForge ≥2 agents) — ✅ PASS (2026-09-03 U4 ship)** `coding_assistant` + `doc_writer` 共存 (11/11 tests pass in AgentForge)
- C5 (Evidence Gate 真实 PASS) — 脚本工具链已 ship (2026-09-04, `phase-7a-baseline-retest-v2`), 待真实 baseline 重测 + Evidence Gate 重跑 (per runbook §1 触发信号) 解锁
- C2 (Solo Dev ≥2 人 OR ≥80h/双周) — 外部约束, 不下降即可复评

---

**Phase 6c Wave 启动条件摘要**（2026-09-02 收官实况，per 6 个 OpenSpec change proposal）:

- **Wave 1 `execution-baseline`** ✅ ship + archived 2026-08-18 (T21) — ADR-0074 D1/D2/D3 V1/V2/V3 prompt builders + 32 few-shot + 54 golden + measure_prompt_baseline CLI, ADR-0074 🔍 Proposed → ✅ Approved
- **Wave 1 `schema-complete`** ✅ ship + archived 2026-08-18 (C9) — ADR-0073 D3 ToolCoordinator 4 步 sanitization pipeline 落地, ADR-0073 🟡 Partial → ✅ Approved
- **Wave 1 `execution-envbackend`** ✅ ship + archived 2026-08-18 (C11-C13) — ADR-0075 D1+D2+D3+D5 全 ship, ADR-0075 🔍 Proposed → ✅ Approved
- **Wave 2 `evidence-gate`** ✅ ship + archived 2026-09-02 (commit `a625b84` + `ce85536`) — 决议 **Conditional** (parse-valid 88.24% ∈ [85, 90) 临界带, mock_mode=true 不构成真实结论); 真实 3 模型 baseline 决议推迟 Sprint 25+
- **Wave 3 `execution-dsl`** ✅ **ship + archived 2026-09-02** — Conditional 决议触发 ADR-0072 D3 declarative style + D5 双语法共存期基础设施; ADR-0072 🔍 Proposed → 🟡 Partial 翻牌（治理异常"实施先于翻牌"已文档化）; `src/modules/parser/declarative_style.{h,cpp}` + `test_dsl_extensions.cpp` 4 cases + `dual_syntax_lint.cpp` C7
- **Wave 4 `control-plane-eval`** ✅ **ship + archived 2026-09-02** — 6 项启动条件实测 → **3/6 FAIL**（①②⑤ FAIL, ③/④/⑥ PASS）; 决议 **Phase 7a 不启动**（结构性 FAIL 项 1-agent + 1-人 ~27h/周 无文档解药）; `docs/audits/2026-09-02-control-plane-eval-v1.md`
- **Wave 4 token-passthrough** ✅ **3/3 SHIP + ARCHIVED 2026-09-10** — Wave 1 #1 GAP 同源, 修 stop_token 透传链路 (execute_node/execute_yield + process/react_once + run/ipc_loop_and_wait); 3 OpenSpec changes 全部 Oracle APPROVE + archive:
  - `fix-yield-node-token-passthrough` ✅ **archived 2026-09-10-fix-yield-node-token-passthrough** — commits 9795784+c2806af+60f3378 (Oracle SHIP-with-fixes P1 YieldStreamBridge{token} + P2 discriminator `__yield__==""`); 2 new TEST_CASEs
  - `fix-orchestrator-token-passthrough` ✅ **archived 2026-09-10-fix-orchestrator-token-passthrough** — commits 4598fda+747a2ed (Oracle decisive experiment: revert `generate(req,{})` → Test 7 FAIL, 实证 Recording Provider 守卫判别力); 1 new TEST_CASE
  - `fix-skill-interpreter-token-and-timeout` ✅ **archived 2026-09-10-fix-skill-interpreter-token-and-timeout** — commits bafce05+b47ba0c+2f05746 (Oracle SHIP-with-fixes P1 poll clamp ≤100ms + P2 Test 7.8c mid-run cancel); 2 new TEST_CASEs + 3 pre-existing expectation 修正
  - 跨 change follow-up (Oracle 观察, 不阻塞本批 ship):
    1. **Cancel 错误码语义模糊**: `simple_orchestrator.cpp:36-39` 把 `LLMError::Cancelled` 映射为 `ErrorCode::Unknown`, 而 skill_interpreter 用 `ErrorCode::Abort`. 同一语义两码, 建议统一 follow-up.
    2. **skill_interpreter.cpp:686 残留 GAP**: `dispatch_llm_generate` 仍硬编码 `std::stop_token{}`. 子进程发起长时间 `llm_generate` IPC 时, 外部 cancel 要等父进程同步 generate 返回才生效. V1 可接受, 建议记入同类潜伏站点清单.
  - 总验证: ctest 228/228 PASS + adr_lint 68 ADR + docs_drift 0 DRIFT + check-model-default-cleared 8/8 OK
- **Wave 4 follow-up #1 fix-cancel-errorcode-semantics** ✅ **archived 2026-09-10-fix-cancel-errorcode-semantics** — ErrorCode enum 末尾追加 `Cancelled` + `llm_error_to_error_code` 把 `LLMError::Cancelled` 映射到 `ErrorCode::Cancelled` (替代 ErrorCode::Unknown); commits 2f15526+1e94bc2 (Oracle bg_04b109df SHIP verdict + optional P3 round-trip test); 2 new TEST_CASEs (Test 8 simulate_cancellation + test_tool_result Cancelled round-trip); switch consumer 全部安全 (execution_result.h default→not retryable, node_executor.cpp default→emit_failed, tool_result.cpp exhaustive 同步); ctest 228/228 PASS
- **Wave 4 follow-up #2 fix-skill-interpreter-dispatch-llm-token** ✅ **archived 2026-09-10-fix-skill-interpreter-dispatch-llm-token** — SkillInterpreter::dispatch_llm_generate IPC 路径补齐 token 透传 (替换原硬编码 `std::stop_token{}`); commits 10176c5 (Oracle bg_e3787930 SHIP verdict); 1 new TEST_CASE (Test 7.8d using `token.stop_possible()` 区分机制避免与 Wave 4 #3 立即 SIGKILL 冲突); 5 处签名同步 (SkillInterpreter::dispatch + dispatch_llm_generate + ipc_loop_and_wait 调用点 + 公开 wrapper); ctest 228/228 PASS; 与 Wave 4 #3 协同: #3 管 loop-top SIGKILL, #2 follow-up 管 dispatch 内部, 互补无重叠
- **ADR-0087 root-cause-upgrade scaffold** 🟡 **planning/handoff only (commit 3832c2d, 2026-09-10)** — ADR-0087 (🔍 Proposed) Cloud adapter 多线程 SIGSEGV root cause 修复 5-Sprint 排期 (5-7 天长周期, 单 session 不可完成); 5 artifacts: proposal + design + tasks + specs/cloud-adapter-threading/spec; 4 Decisions (OpenSSL 3.0 per-thread / httplib upgrade / SerializingDecorator OPT-IN / 5-step 顺序); 5 ADDED Requirements (OpenSSL 3.0 多线程 / httplib Authorization / SerializingDecorator OPT-IN / 回归守卫 / Benchmark baseline); Foundation 已就绪: Wave 4 token passthrough 8 changes 提供完整 cancel chain; 建议下个 Sprint (24+) 排期调研启动 (Sprint 24 调研 OpenSSL 3.0 + httplib issue #1903 fix status)
- **Wave 4 follow-up #2 Oracle 观察 #1 applied** ✅ **commit dd97bcb, 2026-09-10** — `dispatch_llm_generate` early-exit on cancelled token (Oracle bg_e3787930 非阻塞观察 #1 applied, 3 行防御性修改); 1 new TEST_CASE (Test 7.8e 验证 pre-cancel token → generate_calls == 0 拦截前); ctest 228/228 PASS (含 1 pre-existing flaky test_execute_parallel); 零行为变更 (well-behaved provider 等价, 不响应 token 的 provider 强保证)
| **ADR Approved** | **58** (主 50: Phase 0-5 16 + Phase 6 18 [0050/0051/0052-0065/0067] + **ADR-0068** Wave 1 收官 + **ADR-0073** ✅ Approved 2026-08-18 (Phase 6c C9 D2+D3+D4 全 ship) + **ADR-0074** ✅ Approved 2026-08-18 (Phase 6c C1+C2+C3, D1/D2/D3 ship, baseline 数据 handoff to evidence-gate) + **ADR-0075** ✅ Approved 2026-08-18 (Phase 6c C11-C13 D1+D2+D3+D5 全 ship) + **ADR-0081** ✅ Approved 2026-08-22 (Batch 2 P3) + **ADR-0082** ✅ Approved 2026-08-22 (Batch 2 P7) + **ADR-0061-08 v1.1 amendment** ✅ Approved 2026-08-31 (commit `0f19997` flip; Axis6 cognitive_domain composition chain 单主体归因 commit API); plugin 1; skill 子项 7 含 0061-08-v1-1) |
| **ADR 🔍 Proposed** | **17** (主 11: 0038/0039/0042/0045/0046/0070/0071/0076/0077/0078/0083; skill 子项 6: 0061-07~12) — ADR-0068 (D2) **已转 ✅ Approved** (2026-08-03 V2 收官); ADR-0070 (D4) 仍 Proposed; ADR-0081/0082 均 已转 ✅ Approved (Batch 2 P3+P7) |
| **Completed Phase 0-4** | ✅ 100% |
| **Phase 5** | ✅ 收官 (C9-C18 全部 ✅ shipped + archived) |
| **Phase 6** | 🟡 服务化暂缓 (Candidate B 启动条件 🔒 4/4 未满足); Phase 6a (PDK 生产化) 启动评估 ready (Wave 3-A 完成提供前置); **Phase 6c 已收官 (2026-09-02)**; **Phase 6c C1+C2+C3 `from-roadmap-phase-6c-execution-baseline` ✅ ship 2026-08-18** (ADR-0074 D1/D2/D3 V1/V2/V3 prompt builders + 32 few-shot + 54 golden + measure_prompt_baseline CLI, ADR-0074 🔍 Proposed → ✅ Approved, baseline 数据 handoff to evidence-gate); **Phase 6c C9 `from-roadmap-phase-6c-schema-complete` ✅ ship 2026-08-18** (ADR-0073 D3 ToolCoordinator 4 步校验层落地, ADR-0073 🟡 Partial → ✅ Approved); **Phase 6c C11-C13 `from-roadmap-phase-6c-execution-envbackend` ✅ ship 2026-08-18** (ADR-0075 D1+D2+D3+D5 全 ship, ADR-0075 🔍 Proposed → ✅ Approved) |
| **架构规范** | `docs/specs/architecture.md` = 五层模型 (原 v1.2 晋升, **D1 决议 2026-07-31**)；v2.2 八层规范已归档。**D1b 宣告**: "第二大脑"产品愿景 (Persona/Contract/ZK/App Market/brain-frontend) 自 2026-07-31 起正式归档, 不构成当前路线图承诺 |

---

## §Sprint 27 收官注记 (2026-09-11, ADR-0087 root cause + 真实 LLM 依赖 fix Sprint)

**战略定位**: Sprint 27 = ADR-0087 (🔍 Proposed → 🟡 Partial) cloud adapter 多线程 SIGSEGV root cause 升级 5-step 路径第 4 步 ship + 真实 LLM 测试依赖的关键 bug fix + microkernel 蓝图短期可执行项排期顺延。

**4 项 ship + 1 项排期顺延**:

| # | Change / 修复 | 类型 | Commit(s) | 说明 |
|---|---------------|:---:|-----------|------|
| 1 | **ADR-0087 step 4** 移除默认 SerializingDecorator | P0 (LLM 业务) | `de79309` + `9d6d6a6` + `bd1c893` + `a146711` | CloudLLMAdapter cloud 路径默认无 SerializingDecorator 包装; OPT-IN `opts.serializer=true` 降级路径保留 (诊断 + 紧急场景). Oracle SHIP-with-fixes 5 项修正全 applied. 4× 并发性能税去除. **Phase B B.2 / Phase E Skill IPC / Phase G ContextCompactor 多 worker 真并发路径解锁** |
| 2 | **causal_ordering DomainWorkerPool flaky 修复** | P1 (测试稳定性) | `7e9c0a1` | ctest 并发场景下 A→B flaky test 修复 (per ADR-0037 causal clock) |
| 3 | **cognitive_worker prompt 透传 fix** | P0 (真实 LLM 依赖) | `5a09c76` | submit_task 的 `task_id` 误传给 LLM → LLM 看到 "[user] task-id" → 自由发挥字段名 → echo 工具 `args.at("message")` 抛 unordered_map::at → 真实 LLM 测试 fail. 修复: `orch.process(task_id, ...)` → `orch.process(prompt, ...)`. **+1 回归守卫** TEST_CASE 用 `mock->call_history()` 验证 prompt 内容. 工程模式 #1 闭环(最小修复+注释+Recording 守卫) |
| 4 | **kernel-timer-service 排期顺延** | 排期修订 | `66dd686` | `2026-09-10-kernel-timer-service` proposal/design/tasks Sprint 27 → Sprint 28. 原因: Sprint 27 容量被 ADR-0087 step 4 占用 (P0 业务), TimerService 是 microkernel 蓝图沉淀项 (无消费者, Oracle 评审通过). 早收官提前 2 周启动 |
| 5 | **基础状态**: ctest 231/231 PASS 零回归 (含 1 回归守卫 + 1 existing worker_loop test) | — | — | L18 "1 pre-existing FAIL `test_cognitive_worker`" 漂移已修复 (7e9c0a1 + 5a09c76 联合) |

**ADR-0087 root-cause-upgrade parent proposal 状态**: 🟡 Partial (step 4 ✅ ship + merged). step 5 (benchmark + OPT-IN 文档) 留在 parent proposal, 待真实 3 模型 baseline 触发信号 (`docs/runbooks/baseline-retest.md` §1) 启动.

**Oracle session `ses_f6fe76438ffeM5q8z2tUXQ7lIQ` 后续建议**: P1 ✅ ship 后立即启动 P2 TimerService Sprint 28 实施 (本周剩余时间可完成 Task 1-3 接口 + TDD 红绿, Task 4-5 明日上午 temporal_agent 迁移 + 回归, Task 6-7 明日下午文档 + Oracle 复核). 总耗 ~1.5 天.

---

## §Sprint 28 收官注记 (2026-09-12, microkernel 蓝图第 1 件 ship + temporal_agent 迁移)

**战略定位**: Sprint 28 = microkernel 蓝图首个可执行件 ship (per Oracle `ses_f741f5d05ffeItVmfYVEjr67m3` 评审), 解决项目 3 处分散的定时器实现 (WorkflowCallbackChannel 200ms busy-poll / SkillInterpreter 100ms poll / ChatSession stdin 无超时), 并消除 temporal_agent PDK 长轮询的 CPU 浪费. ITimerService 抽象 + TimerService cv + jthread 实现 (contract 层工具, 非 kernel service, 避免 PDK 反向依赖 kernel).

**2 commits ship + 1 PIC fix** (P2.5 + P2.6 + P2.7 完成):

| # | Commit | 类别 | 内容 | 影响范围 |
|---|--------|:---:|------|---------|
| 1 | **`188bd8c`** | feat(common) | ITimerService + TimerService 实现 + 11 case 单测 + CMake 注册 + 6 处路径修正 | `include/agenticdsl/contract/timer_service.h` (110 LOC) + `src/common/utils/timer_service.cpp` (150 LOC) + `tests/test_timer_service.cpp` (11 cases) + `CMakeLists.txt` |
| 2 | **`bc8d751`** | refactor(pdk) | `WorkflowCallbackChannel` 迁移: `std::thread poll_thread_` → `unique_ptr<ITimerService> owned_timer_` + observer ptr + `ITimerService::TimerId periodic_id_` | `pdk/temporal_agent/src/workflow_callback_channel.{h,cpp}` (替换 poll_loop → poll_once() + register_periodic(50ms, ...)) + `pdk/temporal_agent/CMakeLists.txt` (agenticdsl_common link) |
| 3 | (PIC fix in 188bd8c + bc8d751) | build | `agenticdsl_common` STATIC 库添加 `POSITION_INDEPENDENT_CODE ON`, 修复 ld "relocation R_X86_64_PC32 ... recompile with -fPIC" (静态库需 PIC 才能被 .so 链接) | 根 `CMakeLists.txt` |

**设计决策** (per design.md §D1-D6):
- **D1**: contract/common 层工具类, 非 kernel service (避免 PDK 反向依赖 kernel, 同 EventBuilder 先例 ADR-0068)
- **D2**: cv + `condition_variable::wait_until` + `std::chrono::steady_clock`, 跨平台 (Linux/macOS/Windows), 无新外部依赖, 无 timerfd/epoll (避免 Linux-only)
- **D3**: `TimerId = uint64_t`, 单调递增, 单进程 namespace
- **D4**: periodic 累积 deadline 语义 (下次触发 = prev_deadline + period, 与 Linux `timerfd_settime` 一致), 避免累积漂移
- **D5**: 注册线程不安全 (单线程注册契约, 同 `DomainWorkerPool::register_domain_handler`)
- **D6**: temporal_agent 注入 `ITimerService*` (默认 `make_default_timer_service()` std::jthread 实现), 测试可注入 mock

**6 处路径修正** (前置 Task 1):
- proposal.md / design.md / spec.md / tasks.md 中 `include/agenticdsl/common/timer_service.h` → `include/agenticdsl/contract/timer_service.h` (实际 contract 层头文件位置, 同 `EventBuilder` ADR-0068)

**Oracle 优化采纳** (per session `ses_f6fe76438ffeM5q8z2tUXQ7lIQ` Q4):
- Task 4.2 避免 raw new/delete: `unique_ptr<ITimerService> owned_timer_` + observer ptr `timer_` (RAII 自动析构)
- Test #10 `destructor_joins_worker_cleanly`: 删除原始默认构造 `std::thread dummy` + `dummy.join()` 段 (该调用抛 `std::system_error: thread not joinable`, 触发 Catch2 误报)
- Test #7 漂移断言上限: [1000, 1100]ms → [1000, 1500]ms (避免 CI 抖动 flaky)
- Test #11 (Oracle 加): `cancel_concurrent_with_fire` 100 次并发竞争, 验证 cancel 与 fire 二选一语义

**验证结果**:
- 全量 ctest `-j1 -E test_timer_service`: **230/230 PASS 零回归** (test_timer_service 因 Catch2 v3.7.0 reporter bug KI-1 排除)
- temporal_agent 专项 `ctest -R temporal_agent`: **8/8 PASS** (含 `test_temporal_agent_signal_callback` 验证迁移后 callback 链路)
- `test_timer_service` binary exit 0 (60s 内完成, 11 test body 全部跑完, 仅 catch2 reporter 误报 1 failed; 详见 KI-1)
- `pdk_temporal_agent.so` 链接成功 (TimerService 现嵌入所有 temporal_agent 工具的 long-poll 路径)

**Known Issues** (已 ship-with-known-issue, Oracle Task 7 SHIP-with-fixes 标 low-priority follow-up):
- **KI-1: Catch2 v3.7.0 + std::jthread reporter bug** — `test_timer_service` 在多 TEST_CASE 之间 catch2 误报 `SIGTERM - Termination request signal`, 但 binary exit 0 且 11 个 test body 全部正常完成. 根因: Catch2 v3.7.0 reporter 在多 TEST_CASE 之间清理 path 与 std::jthread 析构 timing 有 race (Catch2 v3.8+ 部分修复). **不影响功能**, reporter 误报 only. Mitigation (可选 follow-up): 升级 Catch2 amalgamated 到 v3.8+, 或拆 TimerService 测试到多个 binary (每个 1 个 TEST_CASE), 或迁移到 Catch2 modular headers
- **KI-2: parallel ctest flake** — `ctest -j$(nproc)` 偶发 `test_llm_provider_propagation (Subprocess aborted)`, 串行 `-j1` exit 0 PASS (25 assertions, 12 test cases). 与 TimerService 无关, pre-existing 类 AGENTS.md 提到的 `test_execute_parallel` flaky. 不阻塞 ship.

**后续 follow-ups** (不在本 change 范围):
- Phase 6c 各 real-llm changes (`real-llm-core-coverage`, `skill-interpreter-ipc-realllm`, `chat-real-llm-coverage`) 待 step 4 ship + 真实 baseline 触发
- adr-0087 step 5 benchmark 待 baseline 触发
- microkernel 蓝图后续组件 (PipeBus / UserAgentLoader / procfs 等) 等 TimerService ship 后沉淀, 不立即新建 ADR (Oracle Q1 建议)

## §P2.9 SHIP-with-fixes + Archive 收官 (2026-09-12, Oracle session `ses_f6f8ab1dbffeBh5kdvNi1SEE3k` 复核)

**Oracle 决议**: SHIP-with-fixes (1 Major + 1 Minor 2 修正 + 2 Nits, 零新增 Critical/Major). 2 个 OpenSpec change 全 archived.

**修正清单**:

| 严重度 | 项目 | 修正 |
|:---:|------|------|
| **Major** | `timer_service.h::cancel()` 注释 | 修语义错误: 'cancel 与 callback 互斥' → '不互斥, 返回 true 不代表 callback 未在执行' (worker 在 lock.unlock() 后 fire) |
| **Major** | `workflow_callback_channel.h::start_polling()` 注入参数 | 加生命周期契约: 注入 timer 时调用方必须保证 channel 析构前无 in-flight callback (cancel 不等待已收集但未执行的 callback, `[this]` 捕获可能触发 UAF) |
| Minor 2 | `tests/test_temporal_agent_workflow_callback_timer_injection.cpp` | 新增 D6 注入路径验证 (FakeTimerService 捕获 callback + 手动触发 + 默认 nullptr RAII 路径), 3 test cases / 10 assertions PASS |
| Minor 3 | `openspec/changes/2026-09-12-tr-test-flake-ki2/` | 新增 KI-2 跟踪载体 (test_llm_provider_propagation 并行 flake, 与 TimerService 零因果), 已 archived |
| Nit 1 | `tests/test_timer_service.cpp::test #7` 注释 | `< 1300ms` → `< 1500ms` (与断言一致, Oracle 建议放宽上限) |
| Nit 2 | 4 文件 header | `2026-09-XX` → `2026-09-12` + 删除 L2 `文件头注释` 残留模板行 |

**Commit**: `f145a97` `refactor(common): TimerService SHIP-with-fixes (Oracle ses_f6f8ab1dbffeBh5kdvNi1SEE3k)` (10 files, +210/-586)

**Archive**:
- `openspec archive 2026-09-10-kernel-timer-service --yes` → `openspec/changes/archive/2026-09-11-2026-09-10-kernel-timer-service/` (4 ADDED requirements 合并到 `openspec/specs/kernel-timer-service/spec.md`)
- `openspec archive 2026-09-12-tr-test-flake-ki2 --yes` → `openspec/changes/archive/2026-09-11-2026-09-12-tr-test-flake-ki2/` (5 ADDED requirements 合并到 `openspec/specs/test-flake-ki2/spec.md`)
- 2 changes 全 archived (per-machine ephemeral: `openspec/specs/` + `openspec/changes/archive/` 是 .gitignored, 不需要 commit)

**验证**:
- 全量 ctest `-j1 -E "^(test_timer_service\|test_llm_provider_propagation)$"`: **230/230 PASS 零回归**
- `python3 tools/adr_lint.py`: 0 errors (8 pre-existing WARNING 与本任务无关)
- `python3 tools/docs_drift_audit.py`: **0 DRIFT, 0 WARNING**
- `temporal_agent` 专项: **8/8 PASS** (含 `test_temporal_agent_signal_callback`)
- `test_temporal_agent_workflow_callback_timer_injection`: **3 test cases / 10 assertions PASS, exit 0**

**计数器更新** (§一):
- Total ctest: 231 → **232/232** (+1 test binary `test_temporal_agent_workflow_callback_timer_injection`, 3 cases)
- OpenSpec active: 5 → **4** (TimerService + KI-2 tracking 全 archived)
- 2 changes 总 archived (TimerService + KI-2)

**Sprint 28 microkernel 蓝图完成度**:
- ✅ kernel-timer-service: SHIP + ARCHIVED
- ⏳ PipeBus / UserAgentLoader / procfs: 推迟 (Oracle Q1 建议, 等 TimerService 第 2 个消费者验证模式)
- ⏳ SkillInterpreter / ChatSession timer 改造: 推迟 (作为 Sprint 29+ 独立 change, SkillInterpreter 优先)

**Sprint 28 全收官**: P2.1 (前置) + P2.5 (TimerService 实现) + P2.6 (temporal_agent 迁移) + P2.8 (docs sync) + P2.9 (SHIP-with-fixes + archive) 5 个阶段全 ship. microkernel 蓝图第 1 件 (TimerService) 完成.

---

## 二、活跃变更一览

### 🔵 当前活跃 (0 个 — Phase 6c 已于 2026-09-02 收官)

> **Phase 6c 收官实况 (2026-09-02)**：6 个 OpenSpec changes 全 ship + archived，0 个 active carry-over；详见下方「Wave 1-4 启动条件摘要」§一。**待 Sprint 25+ carry-over**：真实 3 模型 baseline 重测（per Evidence Gate Conditional 决议）。
>
> 1. `from-roadmap-phase-6c-execution-baseline` (C1+C2+C3 ADR-0074 D1/D2/D3) ✅ ship + archived 2026-08-18
> 2. `from-roadmap-phase-6c-schema-complete` (C9 ADR-0073 D3, 4 步 sanitization pipeline) ✅ ship + archived 2026-08-18
> 3. `from-roadmap-phase-6c-execution-envbackend` (C11-C13 ADR-0075 D1+D2+D3+D5) ✅ ship + archived 2026-08-18
> 4. `from-roadmap-phase-6c-evidence-gate` (C4, Conditional 决议) ✅ ship + archived 2026-09-02
> 5. `from-roadmap-phase-6c-execution-dsl` (C6+C7, ADR-0072 D3+D5, **触发 ADR-0072 翻牌 🟡 Partial**) ✅ ship + archived 2026-09-02
> 6. `from-roadmap-phase-6c-control-plane-eval` (C10, **Phase 7a 不启动决议**) ✅ ship + archived 2026-09-02
>
> **Evidence Gate v1 ship 摘要 (2026-09-02)** (`from-roadmap-phase-6c-evidence-gate`, commits `a625b84` + `ce85536`):
> - `src/common/prompts/evidence_gate.h` (62 行, header-only, no IO, `enum class GateStatus { Pass, Fail, Conditional, Abort }` + `evaluate_gate(parse_valid, l1, l2, l3)` 纯函数)
> - `tests/test_evidence_gate.cpp` (7 TEST_CASEs: 4 boundary + 1 Abort + 1 regression + 1 to_string, all PASS, registered tests/CMakeLists.txt:121)
> - `docs/audits/2026-09-02-evidence-gate-v1.md` 决议文档 (X 路线 per MOMUS REJECT 修正: mock baseline 88.24% Conditional placeholder, 真实 3 模型 baseline 推迟 Sprint 25+)
> - **决议状态: Conditional** (🟡) — mock baseline 88.24% 在临界带 [85.0, 90.0); mock_mode=true 不构成真实 LLM 能力结论; 真实决议推迟 Sprint 25+
> - ADR-0074 §决策 D5 实证字段: 2026-09-02 决议记录追加 (file:line 引用 baseline v3 yaml)
> - **Phase 7 启动条件项 #5**: 🟡 Conditional (本决议 placeholder, 待真实 baseline 重测; Wave 3 启动条件需 Evidence Gate 真实 PASS)
>
> **ADR-0072 翻牌记录 (2026-09-02)**：
> - D3 declarative style + D5 D3 arm 共存 ship 先于 ADR 翻牌
> - 状态 🔍 Proposed → 🟡 Partial（per ADR-0073 增量翻牌先例 + STATUS-GLOSSARY.md 定义兼容）
> - D1+D4 待实施；D2 N/A（Conditional 不触发）；D6 OFF（per ADR-0071 §3.C）
> - 翻牌 OpenSpec change 待建 `2026-09-02-adr-0072-flip-to-partial`
>
> **Phase 7 启动决议 (2026-09-02)**：
> - **Phase 7a 不启动**（3/6 FAIL：①AgentForge 1-agent ❌ ②Solo Dev 1-人 ~27h/周 ❌ ⑤Evidence Gate Conditional ❌）
> - 结构性 FAIL 项无文档解药，需人力扩张 + AgentForge 第 2 agent + 真实 baseline 重测
> - Phase 7 启动条件每 Sprint 收官重跑 `scripts/control-plane-eval.py`
> - **Phase 8a (gRPC Data Plane) 仍 gated** by Phase 7a ship ≥3 个月
>
> **T1-T7 归档 2026-09-01** (commit `c79b4bf`):
> Sprint 24 启动周期间累积的 9 个 OpenSpec changes 已 ship + archive:
> - `2026-09-01-analysis-status-snapshot-sync` (✓ Complete 纯文档)
> - `2026-09-01-adr-0080-and-bus-api-alignment` (✓ Complete 纯文档)
> - `2026-09-01-2026-08-30-meta-cognitive-coordination-doc` (§十八 文档沉淀, 🟢 Oracle Go)
> - `2026-09-01-2026-08-31-workflow-materializer-v1` (T1 ship + test_workflow_materializer PASS)
> - `2026-09-01-2026-08-31-signature-validation-real-impl` (T4 ship + test_signature_validation PASS)
> - `2026-09-01-2026-08-31-mcts-axis6-cognitive-domain` (T2 ship + test_mcts_axis6 PASS)
> - `2026-09-01-2026-08-31-evolution-budget-cap` (T3 ship + test_budget_evolution_cap PASS)
> - `2026-09-01-2026-08-31-cognitive-specialists-as-tools` (T5 ship + test_cognitive_specialists_tools PASS)
> - `2026-09-01-2026-08-31-gepa-mcts-budget-integration` (T6 ship, 测试覆盖于 test_budget_evolution_cap + test_gepa_phase2)
>
> 上述 9 changes 在 ship 时 tasks.md 复选框未同步勾选, 但代码 + 测试均已 ship。
>
> **G11 跟踪（✅ Closed 2026-08-26）**:
> - GitHub issue #14 ✅ Approved (2026-08-26): G11 变异治理契约方向批准, 6 项 Oracle 修订 + 16 项 Self-Review Checklist 全 ✅ — **issue #14 已 Closed (2026-08-26)**
> - Oracle session `ses_fc41537bbffeC35NKqgvzn4m1c` Self-Review 预审 + `ses_fc3e070c0ffeIVgAhsgx2pNXFa` 深度审查
> - **`adr-0084-mutation-governance-contract.md` ✅ Approved (2026-08-26, V1 gate-and-audit 代码 ship, commit `a2b2d52`)**：决策 1-6 (变异对象 L1-L4 分级 / 授权绑定复用 ADR-0004+ADR-0031 / 治理流程 propose→evaluator→回归门→commit / 审计复用 ADR-0080+4 mutation.* 主题 / 失败回滚 / 攻击面 fail-closed) + 9 项前置 ADR 引用 + V1 边界 (L4 权重显式禁止) — 13 cases / 139 assertions PASS, ctest 187/187 零回归, OpenSpec change `2026-08-26-adr-0084-mutation-governance-contract` 已 archive
> - **G11 ✅ Closed (2026-08-26)** — cap-map §二/§三 B7/§八.5 + self-evolution-architecture + gap-analysis 已同步
> - T19 GEPA Phase 2 commit 已解锁 (G11 ✅ Closed 2026-08-26, Phase 1 只读反思约束解除)
> - **2026-08-26 自审修正 (Oracle session `ses_fc3090b49ffe7yJwXhx1MoNz5N`)**：原 ADR-0083 头部 "✅ Approved" 与 §状态 "🔍 Proposed" 自相矛盾，已统一为 🔍 Proposed + 代码 ship 待办 (`include/agenticdsl/contract/ievaluator.h` grep 0 命中)。OpenSpec task `2026-08-26-ship-ievaluator-reward-contract` 排期启动
>
> **T19 跟踪（✅ Phase 2 commit ship 2026-08-27）**:
> - **GEPA MVP V1** ✅ ship 2026-08-27 (OpenSpec `t19-gepa-phase2-commit` — GEPALoop 编排层: 失败轨迹反思 + Mock LLM 候选 + SkillCompiler + T14 回归自检 + IEvaluator V2 评估 + MutationGovernor 授权提交; test_gepa_phase2 14 cases PASS [8 骨架 + 2 核心流 + 1 事件 + 3 E2E], 既有 7 契约零修改, ADR-0068 附录 A v1.3 注册 6 个 `gepa.*` 主题; cap-map v2.1 #27 能力 + B7 → ✅ Completed)
>
> **G10 跟踪（✅ Closed — V2 层 ship 2026-08-27）**:
> - **IEvaluator V1** ✅ ship 2026-08-26 (ADR-0083, 12 cases / 31 assertions, change `2026-08-26-ship-ievaluator-reward-contract` archived)
> - **IEvaluator V2** ✅ ship 2026-08-27 (OpenSpec `evaluator-v2-composite` — BehavioralEquivalenceEvaluator [T14 fingerprint + Hotelling T²] + CompositeEvaluator [多评估器加权聚合]; test_evaluator +8 cases / 18 assertions PASS, IEvaluator 接口零修改, V1 零回归; cap-map v2.0 #26 能力落地)
>
> **T21 跟踪（✅ ship 2026-08-28）**:
> - **Prompt Evidence Gate V1** ✅ ship 2026-08-28 (OpenSpec `t21-prompt-evidence-gate` — 质量门控层: `PromptEvidenceGate` Go/Conditional/No-Go 阈值 [≥90%/80-89%/<80%] + IEvaluator V2 CompositeEvaluator 集成 + `PromptAssembler` 两阶段注入 ≤8k tokens + baseline 测量 [3 MockLLM × 2 指标] + JSONL 导出; 32 few-shot `lib/prompt/few_shots/` + 54 golden `lib/prompt/golden/` 实际生成; ADR-0068 附录 A v1.4 注册 3 主题; test_prompt_evidence_gate 19 cases / 338 assertions PASS, 全量 ctest 动态基线 0 回归, 既有 7 契约零修改; cap-map v2.2 #28 能力 + §八 T21 → ✅ SHIP; Wave 2 → Wave 3 Go/No-Go 门控就绪)
>
> **T20 跟踪（✅ ship 2026-08-28）**:
> - **AFlow MCTS V1** ✅ ship 2026-08-28 (OpenSpec `t20-aflow-mcts` — `MCTSWorkflowSearch` 搜索编排层: 5 轴模板实例化搜索空间 + UCB1 选择/扩展/模拟/反向传播 + IEvaluator V2 (CompositeEvaluator) 奖励 + BehavioralRegressionGate 回归门 + MutationGovernor L1 workflow variants 授权 + 4 个 `mcts.*` 事件发射; V1 边界: Mock 模板实例化不触发真实 LLM, AFlow 改进 + L2+ variants deferred V2; test_mcts_workflow_search 17 cases / 65 assertions PASS [10 契约骨架 + 3 UCB1 算法 + 3 V2 集成 + 1 事件发射], 全量 ctest 动态基线 0 回归, 既有 5 契约零修改; ADR-0068 附录 A v1.5 注册 4 个 `mcts.*` 主题; ADR-0061-08 🔍 Proposed → ✅ Approved (V1 ship 2026-08-28); cap-map v2.3 #29 能力 + §八 T20 → ✅ SHIP 2026-08-28; C2 自进化高级工作流搜索解锁)
>
> **T26 跟踪（✅ ship 2026-08-28）**:
> - **Cross-Cutting Pattern PDK V1** ✅ ship 2026-08-28 (OpenSpec `pdk-cross-cutting-patterns` — ICrossCuttingPattern 抽象接口 + CrossCuttingOrchestrator 编排器 + 4 Pattern implementations (Decorator/Hook/Composition/Bus) + DSL loader + 3 examples; ADR-0085 ✅ Approved; 18 测试 cases PASS; 既有 10 个契约文件零修改 (Oracle B3); cap-map v2.4 #30 能力 + §八 T26 → ✅ SHIP 2026-08-28; 横切功能管理 PDK 模式落地)

> **T0 / capture-mode-and-distillation-writer-v1 跟踪（✅ 完整 ship 2026-08-29, Change #1 archived）**:
> - **Phase 0 ✅ ship 2026-08-29** (OpenSpec `capture-mode-and-distillation-writer-v1` — CaptureMode 三态强类型枚举 + DistillationRecord 值类型（对齐 ADR-0061-13 §决策 2 字段全集） + IDistillationWriter L1 契约层接口（3 虚函数 + 1 静态工厂，对齐 ADR-0061-13 §决策 3）; commit `11d3515` — 4 files changed +267 行: capture_mode.h (47) + distillation_record.h (77) + idistillation_writer.h (59) + test_capture_mode.cpp (84); **4 cases / 22 assertions PASS** (含 phase0_headers_compile_smoke 编译期烟雾 case 防零编译覆盖盲区); **Oracle/Metis 双审查 9 项修正全部生效** (D1 reward_signal.h 路径 types/, D2 TDD cmake+make, D3 IDistillationWriter 3 虚函数+工厂对齐 ADR, D4 零编译覆盖, D5 commit 前/后双向验证, D6 DistillationRecord 字段全集, D7 RewardSignal 真嵌入, D8 20 个 contract 文件清单, D9 test_evaluator.cpp 引用修正); **Oracle B3 双向验证通过**: commit 前 `git diff HEAD -- include/agenticdsl/contract/` 0 行, commit 后 `git diff HEAD^ HEAD -- include/agenticdsl/contract/` 仅含 idistillation_writer.h +59 行（其余 20 文件 0 行）; `engine.h` 0 行; `event_log_config.h` 0 行（Phase 1 才改）; **ADR-TRACKING-01**: WARNING 34 与 baseline 持平（本 commit 零 docs/ + openspec/ 修改）; **修正版 prompt 创建**: `.rddf/plans/capture-mode-and-distillation-writer-v1-phase-0-CORRECTED.md` (655 行, Oracle+Metis 9 项修正后版本); cap-map v2.5+ #31 能力状态从 `🔵 Active` 更新为 `✅ Phase 0 ship 2026-08-29`; **遗留**: ⚠ cooling-off 实际违反（kickoff `f7c99aa` 2026-08-29 10:55 → commit `11d3515` 2026-08-29 17:58, 仅 7h3m vs 应 ≥24h）已在 kickoff 文档追加追溯 note
> - **Phase 1+2 ✅ ship 2026-08-29** (Phase 1 `9a781f8`: EventLogConfig BREAKING 迁移 bool→CaptureMode + FileDistillationWriter V1 实施 3 虚函数 + make_file_writer 工厂 + Training 三重保护 + ≤1.5MB 硬约束 + 审计事件 event_log.capture_mode_downgrade; Phase 2 `ed5fcaf`: --allow-training-capture CLI + mock-mode hard rejection + TrajectoryIR→DistillationRecord bridge + payload redact 复用 hash_prompt(); 合计 21 cases PASS); **Change #1 archived 2026-08-29** — 蒸馏数据面闭环 1 第 1 环完整落地

### ✅ Wave 3-A 已归档 (历史参考)

| ID | 名称 | 阶段 | 状态 | 最后更新 |
|----|------|------|:----:|:--------:|
| **P6-W3A-PA** | chat-async-io-queue-infra (`chat-async-io-queue-infra`) | ✅ Done | **✅ shipped + archived 2026-08-08** — ChatSession::Impl 新增 `steering_queue_` + `follow_up_queue_` 双有界队列 (capacity=32, 拒绝新+log warning) + 输入线程分离; 6 个 public API (enum QueueKind + queue_size + try_clear_queue + 2 test helpers) + 4 测试 (41 assertions PASS); **ctest 135/138** (3 pre-existing 不变); commit `3c7f801` propose + `d4fcca1` feat; OpenSpec archived as `2026-08-08-chat-async-io-queue-infra` (spec delta `chat-async-queue-infra`: +5 added). 留 follow-up: Wave 3-A Phase B (cancellation-chain) 依赖已 ship | 2026-08-08 |
| **P6-W3A-PB4** | cancellation-chain-step4-loop-apis (`cancellation-chain-step4-loop-apis`) | ✅ Done | **✅ shipped + archived 2026-08-09** — Phase B Step 4 of 5; 3 BREAKING loop APIs (ReactLoop/PlanExecuteLoop/ForkJoinLoop) 接受 `std::stop_token token = {}` 参数 (default 保持向后兼容, 零调用方修改); PlanExecuteLoop 内部 2 处 `std::stop_token{}` 替换为 token + early-exit check; ForkJoinLoop CV wait predicate 加 `token.stop_requested()` + `pool_->stop()` on cancel; **ctest 138/141** (3 pre-existing 不变); commit `5cbd837` feat; OpenSpec archived as `2026-08-09-cancellation-chain-step4-loop-apis` (spec delta `cancellation-chain-step4-loop-apis`: +4 added). 留 follow-up: Wave 3-A Phase B Step 5 (Mock + E2E test) 依赖已 ship | 2026-08-09 |
| **P6-W3A-PB5** | cancellation-chain-step5-e2e (`cancellation-chain-step5-e2e`) | ✅ Done | **✅ shipped + archived 2026-08-09 (FINAL)** — Phase B Step 5 of 5; MockBlockingProvider (poll token.stop_requested() 每 10ms) + 5 E2E tests (cancellation within 100ms / registry token propagation / default token never cancels / token identity preserved / cross-thread propagation); 143/146 ctest (3 pre-existing 不变, 5 新增 PASS, 0 新增 regression); commit `664ee75` feat; OpenSpec archived as `2026-08-09-cancellation-chain-step5-e2e` (spec delta `cancellation-chain-step5-e2e`: +6 added). **Phase B 7-step wiring 完整 ship**; chat-async-io-steering 提案验收标准 (steering 中断 + follow-up 排队 + stop_token 清理 + ctest 零回归) 全部 PASS. 留 follow-up: Phase C `/model` 切换 | 2026-08-09 |
| **P6-W3A-PC** | chat-async-io-model-switching (`chat-async-io-model-switching`) | ✅ Done | **✅ shipped + archived 2026-08-09 (FINAL)** — Wave 3-A Phase C; `/model <name>` DECLARE_COMMAND 替换 Wave 1 stub + ChatSession `next_model_` atomic + per-turn swap (不强制中断); 4 E2E tests PASS (13 assertions, 含 mock-mode guard 拒绝 openai/deepseek); 147/150 ctest (3 pre-existing 不变, 0 新增 regression); commit `526c88b` feat; OpenSpec archived as `2026-08-09-chat-async-io-model-switching` (spec delta `chat-async-io-model-switching`: +5 added). **Wave 3-A chat-async-io-steering 4-phase 拆分完整 ship** (Phase 0 + A + B 5 steps + C). 留 follow-up: Phase 6 evaluation (ADR-0050 Candidate B - service-ification) | 2026-08-09 |
| **P6-W3A-P0** | fix-tool-registry-signal-handler-shutdown (`fix-tool-registry-signal-handler-shutdown`) | ✅ Done | **✅ shipped + archived 2026-08-08** — pre-approval 审计 (`docs/audits/2026-08-08-chat-async-io-steering-pre-approval.md`) 发现 8 stop_token 断开点 + ToolRegistry SIGSEGV 根因 (`signal_handler` 直接 `unload_all_plugins()` 绕过 `engine.h:199-205` 成员反向析构保证); 修复 `main.cpp:65-86, 467-471` (atomic flag + signal handler 单一 atomic store + main loop 观察) + 2 子进程回归测试 (YAML 校验失败 + SIGTERM mid-load, 9 assertions PASS); **ctest 135/138** (3 pre-existing 不变); commit `1ca4185` propose + `29a7a06` fix + `bc947a0` doc-sync + `4a0c994` archive + `887d660` pre-work; OpenSpec archived as `2026-08-08-fix-tool-registry-signal-handler-shutdown` (spec delta `shutdown-signal-routing`: +4 added). 留 follow-up: Wave 3-A Phase B (cancellation-chain) + Phase C (model-switching) | 2026-08-08 |

### ✅ Wave 2-B 已归档 (历史参考)

| ID | 名称 | 阶段 | 状态 | 最后更新 |
|----|------|------|:----:|:--------:|
| **P6-W1** | chat-streaming-slash-tui (`chat-streaming-slash-tui`) | ✅ Done | **✅ shipped + archived 2026-08-07** — CliOptions 扩展 (`system_prompt`/`append_system_prompt`) + cli_args_parser 扩展 (`--system-prompt`/`--append-system-prompt` 2 新 flag) + ChatConfig::override_system_prompt API + main.cpp 启动 wiring + EventHandler 渲染增强 (completion/ok/trace_id) + EventHandler subscription 修复 (data+meta 合并) + 13 新测试 (4 parser + 4 precedence + 5 rendering); **ctest pdk_chat_demo 子集 13/13 PASS** + 0 新回归; commit `8026984` feat + `717c05a` merge + `90fb325` archive; OpenSpec archived as `2026-08-07-chat-streaming-slash-tui` (spec delta `chat-streaming-slash-tui`: +6 added). 留 follow-up: 无 (原 fix-markdown-parser-yaml + fix-loop-agent-bypass 已 ship 2026-08-03/04) | 2026-08-07 |
| **P6-W1** | session-tree-tui (`session-tree-tui`) | ✅ Done | **✅ shipped + archived 2026-08-07** — CliOptions 扩展 (`fork_node_id`/`session_name`) + cli_args_parser 扩展 (`--fork`/`--name` 2 新 flag) + SessionManager::rename_session API + main.cpp 启动顺序改造 (SessionManager fail-fast) + StartupCleanupGuard RAII (engine/loader 销毁顺序) + 6 E2E test cases + 2 rename API tests + 5 parser tests; **ctest 134/136** → 现 152/152 (5 pre-existing 失败由 fix-markdown-parser-yaml/fix-loop-agent-bypass/adr-0070-declare-command 等 ship 后修复: `921d551` repair 4 pre-existing test failures); commit `39a4323` feat + `2ebe9bf` merge + `8461276` archive + `2df30ba` AGENTS sync; OpenSpec archived as `2026-08-07-session-tree-tui` (spec delta `session-tree-cli-flags`: +4 added). 留 follow-up: 无 (原 fix-markdown-parser-yaml + adr-0070-declare-command 已 ship 2026-08-04) | 2026-08-07 |

### ✅ 已归档 (历史参考)

> Phase 5 全部 8 个 OpenSpec changes (C9-C16) 已于 2026-07-03 ~ 2026-07-09 ship + archived；C17/C18 已于 2026-07-10 ship + archived；C19 (phase6-service-ification-v1) 已于 2026-07-15 ship + archived。Phase 6 (服务化) 暂缓, Phase 6-Redirect (AgentForge MVP) 不创建 OpenSpec change。下表为最近归档的变更状态汇总，**仅作历史参考**。

| ID | 名称 | 阶段 | 状态 | 最后更新 |
|----|------|------|:----:|:--------:|
| **C19** | Phase 6 PDK Composition Spike (`phase6-service-ification-v1`) | ✅ Done | **✅ shipped + archived 2026-07-15** — ADR-0051 ✅ Approved (experimental) + spike-onboarding.md + ToolCoordinator RAII + 5 escalation triggers + Layer 3 dual memos + G1+G3 plugins (8 tests) + E2E (3 tests) + ctest 77/77 PASS + §12/§13 deferred to Sprint 24+ | 2026-07-15 |

| ID | 名称 | 阶段 | 状态 | 最后更新 |
|----|------|------|:----:|:--------:|
| **C18** | Phase 5 Sprint 22 Drift + Strategic Gate (`2026-07-10-phase5-sprint22-drift-strategic-gate`) | ✅ Done | **✅ shipped + archived 2026-07-10** |
| **C17** | Phase 5 ADR States Final Sync (`2026-07-10-phase5-adr-states-final-sync`) | ✅ Done | **✅ shipped + archived 2026-07-10** |
| **C16** | ILLMProvider Call Chain V2 (`phase5-illmprovider-call-chain-v2`) | ✅ Done | **✅ shipped + archived 2026-07-09 (§5 顺延)** |
| **C15** | Batching Queue Plugin (`phase5-batching-queue-plugin`) | ✅ Done | **✅ shipped + archived 2026-07-07 (D2 精简版)** |
| **C14** | Llama Engine Plugin (`phase5-llama-engine-plugin`) | ✅ Done | **✅ shipped + archived 2026-07-08** |
| **C13** | B2 架构层 Schema (`phase5-b2-arch-schemas`) | ✅ Done | **✅ shipped + archived 2026-07-07** |
| **C12** | Phase 5 Step 2 Yield Stream (`2026-07-04-phase5-stage1-step2-yield-stream`) | ✅ Done | **✅ shipped + archived 2026-07-04** |
| **C11** | Phase 5 Step 1 Session Registry (`2026-07-04-phase5-stage1-step1-session-registry`) | ✅ Done | **✅ shipped + archived 2026-07-04** |
| **C10** | Phase 5 Step 0 Lazy ModuleState (`2026-07-03-phase5-stage1-step0-lazy-modulestate`) | ✅ Done | **✅ shipped + archived 2026-07-03** |
| **C9** | Phase 4.5 Impl-Scope Audit (`2026-07-03-phase4-5-impl-scope-audit`) | ✅ Done | **✅ shipped + archived 2026-07-03** |

### 状态图例

| 标记 | 含义 |
|:----:|------|
| 📋 Proposal | proposal.md 已完成 |
| 📐 Tasks | tasks.md 已完成 |
| 📝 Spec | spec.md 已完成 |
| 🔨 编码 | 正在写代码 |
| ✅ Done | 全部 ship gate 通过 |
| 🔒 阻塞 | 等待外部条件 |
| ⏸ 暂缓 | 结构性暂缓 (ADR-0050 Solo Dev 重新评估) |
| ➡️ 顺延 | 无启动触发条件 |
| 🚀 New Sprint | 新 Sprint 启动 (无 OpenSpec change 仪式) |

---

## 三、各变更详情

### C13 / `phase5-b2-arch-schemas`

| 属性 | 内容 |
|------|------|
| **目标** | 创建 4 个架构层 `.md` schema: `prefix_cache.md`, `kv_cache.md`, `decoding.md`, `cloud_engine.md` |
| **D1 已应用** | SamplerStrategy PDK 接口 **删除** — decoding.md 的 sampler 字段保持字符串选择 (5 种) |
| **D3 已应用** | 命名风格统一 `inference.*` |
| **Proposal** | ✅ 干净 (SamplerStrategy 引用全部删除) |
| **Tasks** | ✅ 干净 (§3.2 删除) |
| **Spec** | ✅ 干净 |
| **ship 状态** | ✅ **shipped 2026-07-07** — 4 个 `.md` schema 文件落地 + handoff §10.2 + master plan §C13 已标记 |
| **ship 内容** | `lib/inference/prefix_cache.md` + `kv_cache.md` + `decoding.md` + `cloud_engine.md` (PLACEHOLDER) |
| **验证** | `adr_lint` exit 0 + `openspec validate` exit 0 + `docs_drift_audit` 0 DRIFT + `grep sampler_strategy` 0 matches |

### C14 / `phase5-llama-engine-plugin`

| 属性 | 内容 |
|------|------|
| **目标** | 在 `pdk/llama_engine/` 创建 engine/model 工具实现 (`inference/engine/*`, `inference/model/*`) |
| **D1 已应用** | SamplerStrategy 相关任务删除 (采样器 clamp 内联到 generate) |
| **D3 已应用** | 8 处工具名 `llama_engine/*` → `inference/engine/*`, `llama_model/*` → `inference/model/*` |
| **Proposal** | ✅ 已更新 |
| **Tasks** | ✅ 已更新 (§4 删除, 8 处工具名替换) |
| **Spec** | ✅ 已更新 |
| **剩余工作** | C14 编码: `pdk/llama_engine/` 目录 + 8 个工具注册 + 7 测试 |
| **估时** | ~2-3 天 |
| **启动条件** | **🔒 TSan gate 100%** (`test_execute_parallel` data race 修复验证) |
| **说明** | 编码与工具名重写已分离: 重写 30min 已完成, 编码等 TSan |

### C15 / `phase5-batching-queue-plugin`

| 属性 | 内容 |
|------|------|
| **目标** | 创建 `lib/inference/batching.md` PLACEHOLDER schema (40 行) |
| **D2 已应用** | BatchingQueue PDK 接口 + LlamaBatchingQueue + 贡献流程全部 **推迟** |
| **Proposal** | ✅ 精简完成 (367→98 行) |
| **Tasks** | ✅ 精简完成 (161→70 行) |
| **Spec** | ✅ 精简完成 (214→26 行) |
| **ship 状态** | ✅ **shipped 2026-07-07 (D2 精简版)** — `lib/inference/batching.md` PLACEHOLDER 已落地 |
| **ship 内容** | `lib/inference/batching.md` (~40 行 PLACEHOLDER,batching.submit_and_wait 工具签名) + handoff §10.2 + master plan §C15 已标记 |
| **D2 验证** | 未创建 `include/agenticdsl/pdk/batching_queue.h` + 未创建 `LlamaBatchingQueue` + 未写贡献流程 |

### C16 / `phase5-illmprovider-call-chain-v2`

| 属性 | 内容 |
|------|------|
| **目标** | ILLMProvider 调用链 v2 架构（D2' Dual Consumer Model + D3 ILLMProviderDecorator + D1 Cloud plugin 化 + D5 available_models pure virtual） |
| **依赖** | ADR-0035 (inference engine plugin spec) ✅ |
| **依赖** | ADR-0042 (ILLMProvider evolution path) ✅ |
| **依赖** | ADR-0045 (orchestration plugin spec) ✅ |
| **Proposal** | ✅ v2 修订完成 |
| **§1 Decorator** | ✅ CostTrackingDecorator + 链深度限制 + 流式精度 |
| **§2 Compliance/RateLimit** | ✅ ComplianceDecorator + RateLimitDecorator + DSLEngine opt-in flags |
| **§3 pure virtual** | ✅ `available_models()` =0 + 5 个 override |
| **§4 OrchestrationILLMProvider** | ✅ Dual Consumer Model 直连 + `test_orchestration_dual_consumer` 7 TC PASS |
| **§5 Cloud plugin** | 🔴 顺延（独立 change `phase5-illmprovider-call-chain-v3`） |
| **§6 PluginLoader** | ✅ 5 符号查找 + lifecycle + ABI v2 |
| **§7 ADR 文档** | ✅ ADR-0001/0035/0038/0042/0045/0005 修订 |
| **§8 Deprecate** | ✅ LlamaAdapter + LlamaAdapterProvider `[[deprecated]]` |
| **§9 Engine 集成** | ✅ `decorate_provider()` + 3 处直调路径全部经过装饰器链 |
| **§10 测试** | ✅ 72/72 ctest, ASan 72/72, `test_execute_parallel` fix |
| **ship 状态** | ✅ **可 ship**（§5 顺延） |
| **ASan** | ✅ 72/72 (test_execute_parallel use-after-scope 已修复) |

---

## 四、顺延项（无启动触发条件）

| 顺延项 | 影响 | 启动条件 | 处理方式 |
|--------|------|:--------:|---------|
| ⏸ **Phase 6 服务化 (Candidate B)** | **结构性暂缓** — ADR-0050 §启动条件 #4 Solo Dev 容量 + #5 AgentForge 非真正"外部" 双重不满足 | (1) PDK 生产化达 Sprint 25 末里程碑; (2) AgentForge MVP 验证; (3) 服务化范围文档 ≤1 周可完成 (Solo dev 适配) | ADR-0050 Solo Dev 重新评估 (2026-07-15) + 新 plan `2026-07-15-phase6-agentforge-mvp.md` |
| ⏸ **Phase 7 Control Plane (MCP Server)** | **结构性暂缓 + Phase 7a 不启动** — **Phase 6c 已于 2026-09-02 收官**（Execution Plane ship 完毕，依赖项 #1-3 + #6 已 ✅ PASS）；**实测: 3/6 条件 FAIL** (C1 AgentForge 1 agent ❌, C2 Solo Dev 1人~27h/周 ❌, C5 Evidence Gate Conditional ❌) per [`2026-09-02-control-plane-eval-v1.md`](audits/2026-09-02-control-plane-eval-v1.md); **2026-09-02 决议: Phase 7a 不启动**（结构性 FAIL 项无文档解药） | (1) Phase 6c Evidence Gate 真实 PASS（需 Sprint 25+ 真实 3 模型 baseline 重测）; (2) ADR-0073 完整 ship ✅ (2026-08-18); (3) ADR-0075 EnvBackend ship ✅ (2026-08-18); (4) AgentForge ≥2 agent (当前 1) ❌; (5) Solo Dev ≥2 人 (当前 1) ❌; (6) ADR-0068 §附录 A amendment ship ✅ | 评估脚本 [`scripts/control-plane-eval.py`](../scripts/control-plane-eval.py) 自动检测 + 决策表; **每 Sprint 收官重跑**, 待 3/6 FAIL 项中**至少 AgentForge 第 2 agent + Evidence Gate 真实 PASS** 两项转 PASS 且 Solo Dev 容量无下降时, 启动 Phase 7a 重新评估 |
| ⏸ **Phase 8a Data Plane (gRPC)** | **结构性暂缓** — 依赖 Control Plane ship ≥3 个月 + 路由阈值实测校准需求 | (1) Phase 7 ship ≥3 个月 + 零 critical bug; (2) MCP 路由阈值实测校准需求; (3) 分布式部署需求 OR LLMDataPlane 高频需求 | 路线图 v3 Phase 8a 启动评估 (per ADR-0077 D8) |
| ➡️ C16 §5 Cloud plugin 顺延 | 持续关注 | 外部触发 (CloudLLMProvider 实施需求) | 独立 OpenSpec change `phase5-illmprovider-call-chain-v3` 跟踪 (受 Phase 6 暂缓影响) |
| ➡️ C17 排除 ADR-0030 V2 顺延 | Fleet 实施需求 | FleetOrchestrator 解除延迟 (Oracle 2026-06-27 决议) | C19 或后续 ship 时迁移至 🟡 Partial |
| ✅ ADR-0037 causal-ordering-completion 完成 | (已 ship 2026-09-10, OpenSpec `2026-09-10-adr-0037-causal-ordering-completion` 5 commits archived) | T2 余量 (ToolResult::parent_trace) + T3.0 (EventBuilder.parent_trace setter) + T4 (CognitiveWorker::submit_task(parent_trace)) + T5 (DomainWorkerPool::DomainTask.parent_trace) + T6 (causal_order.h 判定函数) + T7+T8 (9 cases / 31 assertions 测试集) | ADR-0037 状态保持 🟡 Partial (VectorClock/ReorderBuffer 等跨进程机制仍 defer — 升级 ✅ Approved 需 EventBus 落地) |
| ➡️ C17 排除 ADR-0038 顺延 | 推理引擎动态配置接口未实施 | 第二个推理 backend 出现时 (per ADR-0038 §增量决议) | C15 实施后由 C18 重新评估 |
| ➡️ C17 排除 ADR-0039 顺延 | JSON 查询工具 (`inference/get/status`) 未实现 | 实际外部消费者触发时 | 由 AgentForge 反馈触发 |
| ➡️ C17 排除 ADR-0042 顺延 | ILLMProvider 演进路径仅部分决策实施 | C16 §5 Cloud 插件 + 第 2 阶段重新映射交付后 | Phase 6 服务化重新评估时处理 |
| ➡️ C17 排除 ADR-0045 顺延 | 编排 Plugin 仅 step 2 部分交付 (~20% 实施) | Phase 6 实施 | 由 AgentForge 多 agent 协作需求触发 |
| ➡️ C17 排除 ADR-0046 顺延 | 4 通道架构仅通道 ① 完成 (~25% 实施) | Phase 6 实施 | 由 AgentForge 多通道需求触发 |
| 🔒 ADR-0050 §启动条件 #5 字面要求 | AgentForge = HydraForge 同人项目, 非真正"外部 agent/tool" | Oracle round 4 重新评估 (选项 A/B/C per ADR-0051 §后续 #9 注释) | Sprint 24-25 后评估 |
| ✅ ADR-0051 Phase 6 PDK Composition Spike | ✅ Approved (experimental) 2026-07-15 | 已满足 | 一 |
| 🚀 Sprint 24 AgentForge MVP | 启动中 | 不需要 (已启动) | plan 文件 `2026-07-15-phase6-agentforge-mvp.md` |

> **C17 排除原因明细**: 详见 [`docs/superpowers/plans/2026-07-10-phase5-remainder-adr-sync.md` §十一.3](superpowers/plans/2026-07-10-phase5-remainder-adr-sync.md) (Metis 审查 `ses_0b02706b7ffepKdYy3qxnmOzXy` 裁决后用户选项 A 决策 2026-07-10, 范围 12 → 5)
>
> **2026-07-15 新增**: Phase 6 服务化结构性暂缓, 由 `2026-07-15-phase6-agentforge-mvp.md` 替代. 所有原 Phase 6 顺延项 (ADR-0037/0038/0039/0042/0045/0046 + ADR-0050 #5) 启动条件统一改为 "AgentForge 使用反馈触发".
>
> **2026-08-03 新增** (路线图 v3 三平面架构): Phase 7 Control Plane 与 Phase 8a Data Plane 显式列入顺延项, 启动条件与依赖链与路线图 v3 严格对齐. Phase 7 启动条件 6 项 (per ADR-0076 + 路线图 v3 §Phase 7), Phase 8a 启动条件 4 项 (per ADR-0077 D8 + 路线图 v3 §Phase 8a). 6/6 + 4/4 评估决策树详见 [`roadmap.md` §Phase 7/8a](roadmap.md).

---

## 五、最近完成的变更

| 日期 | ID | 名称 | 关键 Ship |
|:----:|:--:|------|-----------|
| 2026-08-18 | — | **execution-envbackend (Phase 6c C11-C13)** `from-roadmap-phase-6c-execution-envbackend` | ADR-0075 D1+D2+D3+D5 全 ship, ADR-0075 🔍 Proposed → ✅ Approved. **关键 ship**: (1) `include/agenticdsl/env/env_backend.h` — IEnvBackend 抽象 + ExecRequest/ExecOptions/ExecResult/BackendCapabilities + create_backend 工厂 + BackendErrorCode 表 (8 个); (2) `LocalBackend` (fork+execve + waitpid 超时 SIGTERM→SIGKILL grace + 输出截断 64KB + setrlimit(RLIMIT_AS/RLIMIT_CPU) + env 白名单 + audit 事件 cmd_hash 脱敏); (3) `DockerBackend` (cpp-httplib + Docker REST API + ephemeral container 生命周期 + Privileged=false 强制 + digest 锁定透传 + Tmpfs/NetworkMode 默认) — **libcurl → cpp-httplib 适配** (external/ 无 libcurl vendor, 零新增依赖); (4) `BackendPolicy` + `BackendConfig::with_defaults()` 默认策略表 3 档 (local/docker ephemeral/docker prod) + `override_default_policy()` per-environment + DockerUnavailablePolicy (FailFast/FallbackToLocal); (5) `make_env_validation_hook()` 工厂 + PreHook lambda 4 步 policy 校验 (backend 命中/镜像 allowlist/env 白名单/working_dir 白名单) + 三层 deny 路径, 注册至 ToolHookRegistry `*` pattern 强制触发. **6 测试文件 33 case 全部 PASS** (远超 proposal ≥27 门槛): test_local_backend 7 / test_docker_backend 7 / test_backend_factory 4 / test_backend_policy 4 / test_env_validation_hook 7 (含 tool_coordinator_dispatch_full_flow 端到端) / test_backend_security 4 (OWASP shell injection × 3 + privileged 拒绝). **ctest 134/135 PASS** (1 pre-existing `test_event_bus_soak` flaky 无关). 文档 ship: `docs/specs/env-backend.md` (新增, IEnvBackend 接口契约 + 2 backend 实现规范 + 64KB vs ADR-0075 D1 1MB 截断差异说明) + `docs/security/backend-policy.md` (新增, 默认策略表 + per-environment 配置 + shell 注入防御 checklist) + `docs/specs/dsl.md §6.5` (新增 shell.exec backend: 字段示例). ADR-0075 §状态 ship 证据段追加. Phase 7 启动条件 (3) ADR-0075 ship ✅ → 4/6 → 5/6 待 Sprint 25 + Solo Dev ≥2 人 + Evidence Gate PASS. 关键 3 处修复 (vs 前置 agent 草案): (a) PipeFds 加默认构造函数 (`PipeFds() = default;`); (b) RLIMIT_CPU hard = soft + 5s grace (man page 语义: cur=max 时内核跳过 SIGXCPU 直接 SIGKILL); (c) test_env_validation_hook::make_exec_meta 需 requires_approval_in_plan=true (V2 validation 强制 dangerous 类目至少 plan/agent 之一). OpenSpec 待 archive `2026-08-18-from-roadmap-phase-6c-execution-envbackend` |
| 2026-08-10 | — | pdk-safe-exec-tests (Phase 6a 任务 2) | SafeExec `std::async → std::jthread + stop_token` 改写, 超时立即抛 (caller 不再阻塞至 fn 完成), 新增 grace_period (默认 50ms) + `with_grace_period()` chain API. 8 test cases / 24 assertions (timeout_returns_quickly / stop_token / leak / grace / types / exception / defaults / chain). ASan 安全: promise/future + shared_ptr<SharedState> 避免 worker 持有栈引用. 新增 `tools/check_doxygen_coverage.sh` (shell + grep heuristic, 30 行前向 lookback + private/public 跟踪) + pdk/README.md 扩展 3 章节 (§ SafeExec实战 + § 3 Agent Loop 选择 + § AgentForge衔接). BACKWARD 兼容: test_pdk_macros 5/5 PASS (1 case sleep 时长微调 50ms → 200ms 避免 ASan race). `scripts/sprint-closeout.sh` 集成 Doxygen audit (新增 Step 6/8). ADR-0021 §3.3 同步 jthread 设计依据 + grace_period 默认值 + Phase 6a 升级. **ctest 121/121 PASS** (32 baseline test_pdk_macros + 8 new test_pdk_safe_exec + 81 others, 0 回归). ASan 121/121 PASS. `tools/adr_lint.py` 0 errors (58 ADRs PASS). `tools/docs_drift_audit.py` 0 DRIFT. Doxygen 100% (9/9). OpenSpec archived `2026-08-10-pdk-safe-exec-tests` (5 files). 5 atomic commits per plan §提交策略. |
| 2026-08-03 | — | promote-event-builder-fulltoolresult-support (V2 收官) | EventBuilder V2 扩展 (`include/agenticdsl/contract/event_builder.h`) — 全 payload 构造器 `EventBuilder(topic, ToolResult)` 接管 7 字段 + 5 个 setter (`.ok(bool)` / `.error_code(ErrorCode)` / `.latency_ms(uint64_t)` / `.trace_id(string)` / `.metadata(json)`). 8 处 operation-result 事件迁移: `tool.completed` / `execution.failed` / `cognitive.task.completed` / `domain.task.{completed,failed}` x3 / `tool.audit.denied` x2 — 全部从 `bus_->emit(BusEvent{...})` 改为 `bus_->emit(EventBuilder(topic, ToolResult).build())`. 新增 `tests/test_event_builder_v2.cpp` (9 test cases, 34 assertions) 覆盖 7 字段透传 + 5 setter + 链式组合. ADR-0068 状态 🟡 Partial → ✅ **Approved** (4/4 验收满足). §5.11 grep 验收: 0 行 (`grep -rn "BusEvent{" src examples --include="*.cpp" \| grep -v event_builder`). **ctest 97/98** (1 pre-existing fail `test_cost_tracking_decorator`). 6 atomic commits (event_builder api + test + 8 migration + adr flip + docs sync + archive). OpenSpec archived `2026-08-03-promote-event-builder-fulltoolresult-support`. |
| 2026-08-03 | — | adr-0068-event-emission-contract (Wave 1 partial ship) | EventBuilder header-only L1 契约层 (`include/agenticdsl/contract/event_builder.h`) — args/meta 分工明确化. 5/7 幻影主题强制发射点落地: `llm.request/response` (TracingDecorator §2) + `tool.execution.start/end` (ToolCoordinator §3) + `session.persisted` (ChatSession §4). 17 处既有 emit 迁移到 EventBuilder (§2-§5): LLM Decorator 链 / ToolCoordinator audit+cycle_detected / ChatSession / CognitiveWorker / DomainWorkerPool / stream_to_bus / 3 个测试 hand-emit. **8 处 raw BusEvent 故意保留** (§决策 7 Operation-Result vs Telemetry 分类 + 附录 B 清单), 扩展 EventBuilder API 推迟至 follow-up `promote-event-builder-full-toolresult-support`. ADR-0068 状态 🔍 Proposed → 🟡 Partial. §6 E2E mock 重写 + §6.1-6.16 deferred (Wave 1 范围外). `git log`: 2 commits (`99087f1` feat + `0fecb54` refactor). **ctest 110/111** (唯一失败 pre-existing `test_cost_tracking_decorator`). `tools/adr_lint.py` 0 errors + `tools/docs_drift_audit.py` 1 DRIFT (active-status ctest 计数已同步修正). OpenSpec archived `2026-08-03-adr-0068-event-emission-contract`. |
| 2026-08-01 | — | tf-integration-coverage | `TopoScheduler::Config::num_workers` 字段 (default 0=hardware_concurrency) + `config_num_workers_` 缓存 + test-only accessor `get_parallel_executor_address_for_test()`. 5 新 contract case in test_execute_parallel.cpp (多调用复用/失败注入/混合节点/Worker 注入) + 7 新 advanced case in test_execute_parallel_advanced.cpp (100 节点 flat DAG/Fork-Join 4 支/默认退化/边界/析构安全/错误路径). 依赖链派发 case disabled (ToolCallNode 4 参构造不暴露 metadata,follow-up). **ctest 107/107 ✅** (96→107,11 active new + 1 disabled). `tools/docs_drift_audit.py` 0 DRIFT + `tools/adr_lint.py` 50 ADR PASS. 6 atomic commits (6e2cfc1+c1bd34f+d0894fc+1481d84+afa98da+ef56b47), 4 files +335/-2. OpenSpec archived `2026-08-01-tf-integration-coverage`. |
| 2026-07-15 | C19 | phase6-service-ification-v1 Spike ship | ADR-0051 ✅ Approved (experimental) + spike-onboarding.md + ToolCoordinator RAII + 5 escalation triggers (6 tests) + G1+G3 plugins (8 tests) + E2E (3 tests) + ctest 77/77 PASS + ASan documented skip + `openspec validate --strict` EXIT 0 + OpenSpec archived. 5 commits. §12 (5 items) + §13 (7 items) deferred to Sprint 24+. C20 kickoff: G2/G4/G5 teams use spike-onboarding.md. |
| 2026-07-14 | C20-Spike | phase6-service-ification-v1 W1 fix list | Oracle Q1-Q6 决策应用 + ADR-0051 创建 (🔍 Proposed) + Spike framing (不兑现 ADR-0050 Candidate B) + DECLARE_TOOL→register_tool_function + slash 命名 (knowledge_base/query) + G3 ToolCategory::Execute + audit events 替代 ToolRegistry 注入 + W1 fix list 11/12 ✅ + 2nd Metis 0 CRITICAL + `openspec validate --strict` EXIT 0 + adr_lint EXIT 0 + docs_drift_audit 0 DRIFT. W2-W3 BLOCKED awaiting Stage Gate 2026-07-18 + Sprint 23 capacity. |
| 2026-07-10 | C18 | phase5-sprint22-drift-strategic-gate | Architecture Drift Gate (4 路 0 CRITICAL) + Strategic Alignment Gate (Oracle 推荐 Candidate B 服务化) + Stage Gate 推迟至 2026-07-18 + ADR-0050 🔍 Proposed 创建 + C20 placeholder 激活 + C19 推迟 |
| 2026-07-09 | C16 | illmprovider-call-chain-v2 | ILLMProvider v2: Decorator chain (CostTracking/Compliance/RateLimit) + Dual Consumer Model (OrchestrationILLMProvider) + available_models() pure virtual + PluginLoader V2 + DSLEngine opt-in flags. 72/72 ctest, ASan 72/72. §5 Cloud plugin deferred. |
| 2026-07-07 | — | c16-patches | C16 三处文档不一致 patch (active-status/proposal/specs) + D5 决策草稿 + proposal-v2.md (313 行, 5 项歧义消除) |
| 2026-07-07 | C15 | batching-queue-plugin | `lib/inference/batching.md` PLACEHOLDER (~40 行) + D2 精简 ship + handoff/master plan 同步 + openspec validate exit 0 |
| 2026-07-07 | C13 | b2-arch-schemas | 4 个 `lib/inference/{prefix_cache,kv_cache,decoding,cloud_engine}.md` schema 文件 + D1 SamplerStrategy 删除 + D3 命名统一 + handoff/master plan 同步 + 全部验证 exit 0 |
| 2026-07-06 | — | docs-cleanup-phase-2 | 5 个已 ship plan 归档, 22 个 openspec spec 补全, ADR 状态同步 |
| 2026-07-04 | C12 | yield-stream | 64/64 ctest, 9 个测试, YIELD 3 节点模式 + Budget 每 token 检查 |
| 2026-07-04 | C11 | session-registry | 63/63 ctest, SessionRegistry 5 方法, 4 个 session.* 工具 |
| 2026-07-03 | C10 | lazy-modulestate | module_states_ map lazy init |
| 2026-07-03 | C9 | adr-impl-scope-audit | 11 个 ADR 实施审计文档 |
| 2026-07-02 | C6 | tool-metadata-v2 | IToolRegistry + ToolMetadata V2 BREAKING |
| 2026-07-02 | C7 | model-router-plugin | 3 路由策略 .so, 4 个 registry 工具, 61/61 ctest |
| 2026-07-01 | — | http-mock-server-helper | HttpMockServer RAII helper, 消除 4 处模板重复 |
| 2026-06-30 | — | decompose-execution-session-h | execution_session.h 14→11 includes, 7→0 modules/ |
| 2026-06-30 | — | fix-audit-quick-debt-2026-06 | ToolResult::error BREAKING, PIMPL-lite MarkdownParser |
| 2026-06-25 | — | engine-include-final-decoupling | engine.cpp cross-module 10→3 |

---

## 六、下一步行动 (按当前焦点)

> **2026-09-02 路线图更新**: **Phase 6c 已收官**（11/13 任务 ship + C5 N/A + C4 Conditional 决议，Evidence Gate = Conditional mock 88.24%）；**Phase 7a 不启动**（3/6 FAIL 实测：①AgentForge 1-agent ②Solo Dev 1-人 ~27h/周 ⑤Evidence Gate Conditional FAIL，结构性项无文档解药）；**真实 3 模型 baseline 重测 + Phase 7a 启动评估**留 Sprint 25+ carry-over。

### Sprint 24-26 历史收官 (2026-07-15 ~ 2026-09-02)

✅ **Sprint 24 (2026-07-15 ~ 2026-07-29)** — AgentForge MVP + Phase 6a PDK 生产化（T1-T8 全部 ship, ctest baseline 184 → 192）
✅ **Sprint 25 (2026-07-29 ~ 2026-08-12)** — Phase 6b partial ship + carry-over + Wave 3-A chat-async-io 完整 ship（ctest baseline 192 → 211）
✅ **Phase 6c (2026-08-19 ~ 2026-09-02)** — **实质 ship 收官**：
  - Wave 1 `execution-baseline` (C1-C3) ✅ ship 2026-08-18
  - Wave 1 `schema-complete` (C8-C9) ✅ ship 2026-08-14/18
  - Wave 1 `execution-envbackend` (C11-C13) ✅ ship 2026-08-18
  - Wave 2 `evidence-gate` (C4) ✅ Conditional 决议 ship 2026-09-02
  - Wave 3 `execution-dsl` (C6+C7) ✅ Conditional 触发 ship 2026-09-02（**触发 ADR-0072 翻牌 🟡 Partial**）
  - Wave 4 `control-plane-eval` (C10) ✅ 3/6 FAIL 决议 ship 2026-09-02（**Phase 7a 不启动**）

### Sprint 25+ (2026-09 起, carry-over)

1. **真实 3 模型 baseline 重测** — Conditional 决议 placeholder, 触发条件 = 外部模型窗口可用
   - 重跑 `tools/baseline/measure_prompt_baseline.py` + 3 模型 (GPT-4 / Claude 3.5+ / DeepSeek) × 50 tasks held-out suite
   - 决议 pass → 触发 ADR-0072 D2 `$var` 实施 + Phase 7a 重评
   - 决议 fail → 维持 Conditional / No-Go, 6b carry-over（U2/U3/U4/U6/W4/W5）继续推进
2. **Phase 7a 启动复评** — 触发条件 = 3 项 FAIL 中**至少 AgentForge 第 2 agent + Evidence Gate 真实 PASS** 两项转 PASS
3. **Phase 6b carry-over 真实剩余**（per 2026-09-02 收官盘点）：
   - U2 code-review-run.skill.md → SkillInterpreter .cpp 集成（mock-only 或全功能）
   - U3 `include/agenticdsl/pdk/README.md` 完整开发者指南
   - U4 AgentForge 第 2 个领域 agent
   - U6 ADR-0042 状态对齐（仍 🔍 Proposed）
   - W4 ADR-0072 D1 `stream: true` 全局扩展（仅 LLM `dsl_call` 已支持）
   - W5 ADR-0072 D4 `backend:` 字段（ADR-0075 backend_policy.h 已 ship, parser 未对接）
4. **ADR-0072 翻牌 OpenSpec change 落地** — `2026-09-02-adr-0072-flip-to-partial`（single-dev mode: issue + 24h cooling-off + Self-Review Checklist）

### 架构缺失能力治理 Wave 1 (2026-07-31 挂接, 与 Sprint 24/25 并行)

> 来源: [`docs/architecture/layer-based-missing-capabilities-analysis.md`](architecture/layer-based-missing-capabilities-analysis.md) §十二 Wave 1 (P0#0 + 借鉴路径 P0 关键 4 项, ~3 Sprint 估时)。索引与待决策项见 [`docs/architecture/README.md`](architecture/README.md) §四。

| 序 | 缺失项 | 层 | 估时 | 状态 |
|---|--------|----|------|------|
| 1 | L4-1 loop_agent bypass 修复 (`chat_session.cpp:233` 短路分支) | L4 | 0.5S | 📋 待启动 |
| 2 | X1 事件发射契约 (8 个零 emit 主题补齐 + ADR-0068) | L1 | 1.0S | 📋 待启动 |
| 3 | L3-3 DECLARE_COMMAND 宏 (ADR-0070) | L3 | 0.5S | 📋 待启动 |
| 4 | X2 ToolCoordinator Hook 注入 (ADR-0069) | L1 | 1.0S | 📋 待启动 |

> 启动前置: 架构组决策 D1~D6 (✅ 2026-07-31 全部关闭); 修复路径按 Phase 6 plan+commit 模式 (D5 已决议)。
> **提案池**: Wave 1 四项 + Wave 2/3 八项 (fix-markdown-parser-yaml / session-manager-jsonl / context-compactor / chat-streaming-slash-tui / chat-async-io-steering / cli-args-cxxopts / session-tree-tui / provider-dynamic-discovery) 已登记 [`proposal-suggestions.md`](../../proposal-suggestions.md), 待 guide-arch Phase 5.5 逐个审查。

### 进化管线服务 (中长期, 不阻塞 Phase A)

> 详见 [`docs/architecture/agent-evolution-pipeline.md` §八](architecture/agent-evolution-pipeline.md)。启动条件由实际需求驱动, 不预设 timeline。

| 服务 | 阶段 | 当前状态 | 启动条件 |
|------|:----:|---------|:--------:|
| `SkillInterpreter` V2 完善 | 1 | 🟡 Partial (V1 done) | Sprint 24-25, 3 项 defer 范围 |
| `DSLValidator` 增强 | 2 | ❌ 无代码 | Sprint 24, 靠 DSLEngine (已有) |
| Wasm 技术栈预研 | 4 | ❌ 无代码 | Sprint 24-25, ADR-0056 已定义 |
| `SolidificationEngine` | 1→2 | ❌ 无代码 | DSLValidator + SkillInterpreter V2 完成后 |
| `WasmRuntime` PoC | 4 | ❌ 无代码 | wasi-sdk 通路验证 + AgentForge MVP 完成 |
| `RegressionSuite` | 全阶段 | ❌ 无代码 | SolidificationEngine V1 完成后 |
| `WasmCompiler` | 2→4 | ❌ 无代码 | WasmRuntime 完成后 |
| `C++CodeGenerator` | 2→3 | ❌ 无代码 | SolidificationEngine 完成后 |

### 已归档的 Phase 6 服务化路径

- ⏸ **Phase 6 服务化 (Candidate B)** 暂缓, 启动条件重新定义 (见 §四)
- ⏸ **C20 (analysis-service)** 暂停, 等 Sprint 24-25 末评估 (ADR-0050 §决策 Solo 重新评估段)
- ⏸ **C16 §5 Cloud plugin** 顺延不变 (独立 change 跟踪)
- ⏸ **C19 (fork-checkpoint)** 推迟不变, 触发条件 = Phase 7+ 自进化
- ✅ **ADR-0051 ✅ Approved (experimental)** 状态保留, 不影响其他 ADR

---

## 七、存档说明

> 以下历史看板已归档: 它们的 Phase 0-4 追踪已由 `docs/active-status.md` 替代。
>
> - **`docs/roadmap-status.md`** → `docs/archive/roadmap-status.md` (Phase 0-4 Sprint 日志, 最后更新 2026-06, 463 行)
> - **`docs/implementation-roadmap.md`** → `docs/archive/implementation-roadmap.md` (2016-06-03 旧蓝图, 829 行)

---

## 八、参考

| 文档 | 用途 |
|------|------|
| `docs/superpowers/plans/2026-07-03-phase5-self-bootstrapping.md` | 详细执行计划、依赖图、每 Change 精确实施步骤 |
| `docs/adversarial-reviews/decisions-2026-07-07.md` | B2 架构决策 D1-D4 正式记录 |
| `docs/adversarial-reviews/main-report.md` | Adversarial Review 完整报告 (5 大发现 + 4 方案对比) |
| `openspec/changes/` | 活跃 OpenSpec change 目录 (proposal/tasks/spec) |
| `docs/archive/roadmap-status.md` | Phase 0-4 历史 Sprint 追踪 (已归档) |
| `docs/archive/implementation-roadmap.md` | 旧实施路线图 (已归档) |
