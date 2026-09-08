## Why

`chat-real-llm-coverage` (sibling change, ship in-progress) 解决了 `pdk_chat_demo` 内的 LLM 真实端到端测试覆盖。但仓库内还有 **10 个 LLM 调用点** 仅有 mock 测试,真实 LLM 行为下未被验证:

1. **simple_orchestrator.cpp:118** — ReAct `generate(req, {})` — 硬编码 prompt 要求 LLM 输出 `{"tool": ...}` JSON,真实 deepseek 是否产出合法 JSON 决定 CognitiveWorker 链路可用性
3. **plan_execute_loop.h:217/268** — plan/verify `generate(req, token)` — verify 阶段 LLM 是否返回含 "yes" 决定循环正确性,plan 阶段 LLM 是否产出合法 DSL 决定 ExecutePhase 可执行性
4. **cost_tracking_decorator** — 计费精度依赖 usage→tokens 映射 (cloud_adapter.cpp:344-349),真实 LLM 下 token 数 > 0 验证 budget 扣费正确
5. **skill_interpreter.cpp:659** — Skill IPC llm_generate,父进程 IPC 线程无超时,慢 LLM 可能永久卡死
6. **node_executor.cpp:576** (YieldNode) — `generate_stream(req, {})` — 取消传播硬编码空 token,流式生成无法中断
7. **gepa_loop.cpp:116** — GEPA 反射,LLM 生成 skill 编译失败已有优雅路径 (continue)
8. **context_compactor.cpp:64** — 摘要 LLM 调用,`count_tokens` 仍是 TODO

**用户原始关注**: "交了 LLM 后延时会变长,需要测试来验证能工作" — 这是核心动机。

## What Changes

### Scope: core 测试树 (非 examples/)

- `tests/test_cognitive_worker.cpp` — 扩展,加真实 LLM ReAct JSON 契约 (CognitiveWorker 端到端)
- `tests/test_domain_worker_pool.cpp` — 扩展,验证并发共享 provider 线程安全 + 限速
- `tests/test_plan_execute_loop_integration.cpp` — 加 verify "yes" 真实 LLM
- `tests/test_cost_tracking_decorator.cpp` — 验证真实 token 计费精度
- `tests/test_skill_interpreter.cpp` — 加真实 LLM llm_generate IPC 路径
- `tests/test_yield_node.cpp` (新) — 流式取消 (需先补 token 透传)
- `tests/test_context_compactor.cpp` (新) — 摘要真实 LLM

### 取消覆盖断裂 (本 change 不在范围)

以下 LLM 调用点**当前用 `std::stop_token{}`** 而非转发真实 token — `/cancel` 无法中断它们的 in-flight 调用:

- simple_orchestrator.cpp:118 ❌
- node_executor.cpp:576 (YieldNode) ❌
- gepa_loop.cpp:116 ❌
- skill_interpreter.cpp:659 ❌
- context_compactor.cpp:64 ❌

**修复取消透传属于生产代码变更**,超出本 change scope。需另立 fix-up change:
- `fix-yield-node-token-passthrough` (NodeExecutor change)
- `fix-orchestrator-token-passthrough` (CognitiveWorker change)
- `fix-skill-interpreter-token-and-timeout` (SkillInterpreter change)

本 change 在 test 中**显式记录**这些断裂,作为后续修复的验收点。

### 共享 helper 复用

复用 `chat-real-llm-coverage` 的 `examples/pdk_chat_demo/tests/test_helpers/real_llm_env.h`:
- 需迁移到 `tests/test_helpers/real_llm_env.h` (项目级)
- 同样 env var 真值表 (SKIP=1 → skip / key set → run / 无 key → FAIL)
- 同样错误码 (`AuthenticationError` / `NetworkError`)

## Scope Boundaries (In)

- ✅ mock → real LLM 行为验证
- ✅ 真实 LLM 错误码断言 (修正版)
- ✅ 真实 LLM 延迟下系统仍可工作 (核心承诺)
- ✅ 并发共享 provider 线程安全
- ✅ 取消覆盖断裂**记录**(不修复)

## Scope Boundaries (Out)

- ❌ 修改生产代码 (token 透传 / 超时保护) — 另立 fix-up change
- ❌ pdk_chat_demo 范围内 LLM 测试 — 已在 `chat-real-llm-coverage`
- ❌ `/model` 运行时 provider 切换 — 在 `chat-model-switch-real`
- ❌ 真实 LLM 流式测试 (需先补 YieldNode token 透传)

## Impact

**受影响的测试**:
- `tests/test_cognitive_worker.cpp` (扩展, mock → real mix)
- `tests/test_domain_worker_pool.cpp` (扩展)
- `tests/test_plan_execute_loop_integration.cpp` (扩展)
- `tests/test_cost_tracking_decorator.cpp` (扩展)
- `tests/test_skill_interpreter.cpp` (扩展)
- 新增 `tests/test_yield_node.cpp` (Phase A — 需先补 token 透传)
- 新增 `tests/test_context_compactor.cpp` (Phase B)

**预计新增**:
- 14 cases (P0: 8, P1: 6)
- helper 迁移 + 自测 4 cases
- ctest 总数: 240 → ~258

**CI 影响**:
- 当前 CI 默认 examples=OFF → 本 change 测试在 core 树,会被构建运行
- 需 ci.yml 默认 `HYDRAFORGE_SKIP_REAL_LLM: "1"` (前瞻, 已有 sibling change 待办)
- 真实 LLM 验证走 `workflow_dispatch` 手动 job

**Non-goals**:
- ❌ 不修改 production 代码 (non-token-passthrough fixes)
- ❌ 不实现 `/model` 切换
- ❌ 不为 pdk_chat_demo 添加新测试 (在 sibling)

## 验证标准

- `cmake --build build -j$(nproc)` 编译通过 (0 error, 0 warning)
- `ctest -R "test_cognitive_worker|test_domain_worker_pool|test_plan_execute|test_cost_tracking|test_skill|test_yield|test_context_compactor" --output-on-failure` 全绿
- 无 API key 时这些真实 LLM 测试 → FAIL (硬门槛)
- 全量 `ctest -j$(nproc)` 223 → ~258 baseline 不引入 regression
- `openspec validate real-llm-core-coverage --strict` exit 0
- `tools/adr_lint.py` 0 errors
- `tools/docs_drift_audit.py` 0 CRITICAL drift

## 估时

| Phase | 内容 | 估时 |
|---|---|---|
| 0 | helper 迁移 (pdk → project) + 自测 | 30 min |
| A | CognitiveWorker ReAct JSON 契约 (P0) | 45 min |
| B | DomainWorkerPool 并发 + 线程安全 (P0) | 30 min |
| C | PlanExecuteLoop verify "yes" (P0) | 30 min |
| D | CostTrackingDecorator 真实计费 (P0) | 30 min |
| E | SkillInterpreter IPC llm_generate (P1) | 45 min |
| F | YieldNode 流式取消 (P1, 需先补 token) | 30 min |
| G | ContextCompactor 摘要 (P1) | 30 min |
| H | 验证 + archive | 15 min |
| **Total** | **~5-6 小时** (含取消覆盖断裂 fix-up 阻塞风险) |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| 取消覆盖断裂阻塞部分测试 | 显式记录为 fix-up,测试中以 `INFO()` 标记已知断裂点 |
| SkillInterpreter IPC 无超时 → 慢 LLM 卡死 | 本 change 测快速路径,慢路径单独隔离测试 (可选 `[realllm][flaky-acceptable]` tag) |
| 共享 provider × N worker 线程安全 | cloud_adapter 每次新建 httplib::Client 疑似安全,本 change 用 1×4 实证 |
| count_tokens TODO | 接受 fallback (字符数近似) |
| 与 `chat-real-llm-coverage` helper 同步 | helper 升级时双向同步,记录差异 |
| ci.yml 默认 skip 配置未生效 | 与 sibling change 同步添加 `HYDRAFORGE_SKIP_REAL_LLM=1` env |