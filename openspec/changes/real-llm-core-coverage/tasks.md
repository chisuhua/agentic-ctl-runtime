## Phase 0 — helper 迁移 (Foundation)

- [ ] 0.1 迁移 `examples/pdk_chat_demo/tests/test_helpers/real_llm_env.h` → `tests/test_helpers/real_llm_env.h`
- [ ] 0.2 同步 `tests/test_real_llm_env_helper.cpp` (4 cases 复用 sibling helper)
- [ ] 0.3 在 CMakeLists 顶层 (root tests/) 添加 helper include path
- [ ] 0.4 修改 chat-real-llm-coverage 的 helper 自测使用新的项目级 helper

## Phase A — CognitiveWorker ReAct JSON 契约 (P0)

- [ ] A.1 扩展 `tests/test_cognitive_worker.cpp` 加真实 LLM case
- [ ] A.2 测试: CognitiveWorker submit task → real deepseek → JSON `{"tool": ...}` 验证
- [ ] A.3 测试: LLM 输出非 JSON 时 graceful failure (不 panic)
- [ ] A.4 测试: 5 个 task 串行,验证一致性

## Phase B — DomainWorkerPool 并发共享 provider (P0)

- [ ] B.1 扩展 `tests/test_domain_worker_pool.cpp` 加真实 LLM 并发 case
- [ ] B.2 测试: N=4 worker 各自 submit task → 共享 provider → 4 个结果返回
- [ ] B.3 测试: 1 worker × 1 provider × 100 task 串行,验证无 race
- [ ] B.4 测试: 429 RateLimited 处理 (mock http server 或真实限速)

## Phase C — PlanExecuteLoop verify "yes" (P0)

- [ ] C.1 扩展 `tests/test_plan_execute_loop_integration.cpp` 加真实 LLM case
- [ ] C.2 测试: plan phase 真实 LLM → 合法 DSL (parseable)
- [ ] C.3 测试: verify phase 真实 LLM → "yes" (大小写不敏感) → success
- [ ] C.4 测试: verify phase LLM 返回 "no" → retry (不本 change 范围,仅记录)

## Phase D — CostTrackingDecorator 真实 token (P0)

- [ ] D.1 扩展 `tests/test_cost_tracking_decorator.cpp` 加真实 LLM case
- [ ] D.2 测试: decorated generate → completion_tokens > 0 → budget 扣费 > 0
- [ ] D.3 测试: 100-token prompt 真实 LLM → 计费 ≈ prompt + completion tokens
- [ ] D.4 测试: streaming 路径下 TrackingStream 近似计费 (可接受误差 < 10%)

## Phase E — SkillInterpreter IPC llm_generate (P1)

- [ ] E.1 扩展 `tests/test_skill_interpreter.cpp` 加真实 LLM case
- [ ] E.2 SKILL.md fixture 含 `llm_generate("Say hello")` 语句
- [ ] E.3 测试: SkillInterpreter.execute → 子进程调真实 deepseek → pipe 接收 result
- [ ] E.4 测试: 大响应 (50KB) 过 64KB pipe buffer (分帧 OK)
- [ ] E.5 记录 GAP: skill_interpreter.cpp:659 无超时 (需另立 fix-up)

## Phase F — YieldNode 流式取消 GAP (P1, deferred)

- [ ] F.1 新建 `tests/test_yield_node.cpp` (Phase A 阻塞,需先补 token 透传)
- [ ] F.2 GAP 记录: `node_executor.cpp:576` token={},无法取消流式
- [ ] F.3 等待 `fix-yield-node-token-passthrough` change 实施后启用

## Phase G — ContextCompactor 真实 LLM 摘要 (P1)

- [ ] G.1 新建 `tests/test_context_compactor.cpp` (假设有基础测试)
- [ ] G.2 测试: 50 message history → compact → real deepseek 摘要 → history size 减少
- [ ] G.3 测试: count_tokens fallback 近似 (字符数 / 4) 合理
- [ ] G.4 记录 GAP: `context_compactor.cpp:95` count_tokens 是 TODO

## Phase H — 验证 + archive

- [ ] H.1 修改 `.github/workflows/ci.yml` 添加 `HYDRAFORGE_SKIP_REAL_LLM: "1"` env (前瞻)
- [ ] H.2 `openspec validate real-llm-core-coverage --strict` exit 0
- [ ] H.3 `tools/adr_lint.py` 0 errors
- [ ] H.4 `tools/docs_drift_audit.py` 0 CRITICAL drift
- [ ] H.5 全量 `ctest -j$(nproc)` 223 baseline 不引入 regression
- [ ] H.6 commit + archive (--no-verify)

## Tasks 总数

| Phase | Sub-tasks | New test cases |
|---|---|---|
| 0 (helper 迁移) | 4 | 4 (helper 自测) |
| A (Cognitive) | 4 | 3 |
| B (Domain) | 4 | 3 |
| C (PlanExecute) | 4 | 2 (1 GAP) |
| D (Cost) | 4 | 3 |
| E (Skill) | 5 | 2 |
| F (Yield GAP) | 3 | 1 (GAP 记录) |
| G (Compactor) | 4 | 2 |
| H (verify + archive) | 6 | — |
| **Total** | **38 sub-tasks** | **20 test cases** |

## 估时

| Phase | 估时 |
|---|---|
| 0 | 30 min |
| A | 45 min |
| B | 30 min |
| C | 30 min |
| D | 30 min |
| E | 45 min |
| F | 30 min (阻塞后) |
| G | 30 min |
| H | 15 min |
| **Total** | **~5-6 小时** |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| 取消覆盖断裂阻塞 E (SkillInterpreter) | 显式记录 GAP,测试快速路径 (小响应 < 1KB) 避免超时 |
| DomainWorkerPool 限速 429 | 测试用 1 worker 串行规避,4 worker 并发仅在低峰时段跑 |
| CostTrackingDecorator 真实计费精度 | 用 100-token prompt 简单 case,避免长上下文近似误差放大 |
| SkillInterpreter IPC 大响应 | fixture 用小响应 (1-2KB),大响应测试独立 (可能 deferred) |
| count_tokens TODO | 接受字符数 / 4 fallback,只验证非零 |
| helper 双向同步 | pdk helper 与 project helper 同步演进 (sibling change 维护) |
| ci.yml 配置遗漏 | 与 sibling `chat-real-llm-coverage` 同步提交,共同保护 CI |