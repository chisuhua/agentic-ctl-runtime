# Spec: real-llm-core-test-coverage

## Purpose

为非 `pdk_chat_demo` 路径的 10 个 LLM 调用点建立生产级真实 LLM 测试覆盖。

## ADDED Requirements

### Requirement: CognitiveWorker ReAct JSON 契约 (P0)

`SimpleCognitiveOrchestrator::react_once()` (simple_orchestrator.cpp:110-118) 硬编码 prompt 要求 LLM 输出 `{"tool": ...}` JSON 格式。真实 LLM 测试 SHALL 验证 deepseek 是否在真实延迟下产出合法 JSON,触发正确的工具链。

#### Scenario: CognitiveWorker ReAct produces valid JSON tool call
- GIVEN real deepseek configured
- AND CognitiveWorker with handler tool registered
- WHEN submit task requiring tool use
- THEN result.response 包含合法 JSON `{"tool": <name>, "args": {...}}` (或 schema 类似)
- AND tool 实际被调用
- AND no panic (LLM 输出非 JSON 不应 crash)

#### Scenario: Model default value SHALL not shadow provider config (found & fixed)
- GIVEN `LLMParams = LLMConfig` 别名 (llm_types.h) 且 `LLMConfig::model` 默认 `"gpt-4o-mini"` (llm_config.h)
- AND SimpleCognitiveOrchestrator::react_once 构造 `GenerationRequest req` 不设 params.model
- WHEN real deepseek provider (config.model = deepseek-v4-flash) 收到请求
- THEN CloudLLMAdapter L164 (`req.params.model.empty() ? config_.model : req.params.model`) 原会取非空默认 "gpt-4o-mini" 遮蔽真实 model
- AND server 拒绝 (`"you passed gpt-4o-mini"`) → 真实 LLM 测试 FAIL
- FIXED: react_once 增加 `req.params.model.clear()` → adapter fallback config_.model
- AND A.2/A.4 真实 deepseek 测试 PASS (sibling test_e2e_real_llm 显式设 model 佐证)
- AND A.5 回归守卫 (test_simple_orchestrator RecordingLLMProvider) 断言 model 为空,
  CI (skip=1) 下确定性拦截该修复回归 (Oracle P1-1)

#### Scenario: Model shadowing is systemic — other sites SHALL be tracked (Oracle P1-2)
- GIVEN `req.params.model.clear()` 修复仅覆盖 simple_orchestrator.cpp
- AND 同类潜伏站点: node_executor.cpp:356 (ll_call) / :574 (YieldNode) /
  skill_interpreter.cpp:657-659 (IPC) / context_compactor.cpp:60 (摘要) / gepa_loop.cpp:115 (反射)
- WHEN Phase E/G 在这些路径上启用真实 LLM (不经 orchestrator)
- THEN 首个用例报 "you passed gpt-4o-mini" (预期红, 非环境问题)
- AND 由独立跟进 change `fix-generation-request-model-default` 系统性修复
  (Phase B-G 暴露 ≥3 站点 → 升为 Phase B 前置 P0)
- AND telemetry 副作用已文档化: decorator 链 (cost/compliance/tracing) 在 orchestrator
  路径记录 model="" — 不影响 token 扣费, Phase D (cost) 前确认

### Requirement: DomainWorkerPool 并发共享 provider (P0)

`DomainWorkerPool` N 个 std::jthread worker 各自通过共享 provider 实例并发 generate (N=4)。真实 LLM 测试 SHALL 验证并发线程安全 + 限速合理性。

#### Scenario: 4 workers concurrent generate via shared provider
- GIVEN real deepseek provider shared across DomainWorkerPool with N=4 workers
- WHEN each worker calls generate independently
- THEN all 4 results return successfully (no race)
- AND no httplib::Client corruption error
- AND 429 RateLimited handling graceful (no infinite retry loop)

#### Scenario: Multi-thread httplib SIGSEGV (known issue, deferred to fix-up change)
- GIVEN CloudLLMAdapter + N≥2 worker 并发 + Authorization header (api_key set) + https
- WHEN DomainWorkerPool handler 调 shared provider
- THEN httplib::Client::Post 内部 create_client_socket SIGSEGV
  (socket_options_ 栈 corruption; gdb backtrace: ~ClientImpl → ~basic_string → 空地址)
- AND 单线程 pool(1) + 真实 deepseek PASS (A.2 已 ship, B.2 pool(1) 实证)
- AND 无 Authorization mock provider 路径 PASS (B.3/B.4 mock simulate PASS)
- DEFERRED to `fix-cloud-adapter-multithreading` change (OpenSSL/SSL_CTX 多线程 init
  或 httplib Authorization header 栈修复, 超出本 test-coverage change scope)
- 测试用 Catch2 SKIP macro 暂跳过, 不阻塞本 change ship

### Requirement: PlanExecuteLoop verify "yes" (P0)

`plan_execute_loop.h:252-268` verify 阶段调 LLM 断言含 "yes" 响应。真实 LLM 测试 SHALL 验证真实 deepseek 在延迟下产出合规 verify 响应。

#### Scenario: PlanExecuteLoop verify "yes" path
- GIVEN real deepseek PlanExecuteLoop
- AND plan phase returns valid DSL
- WHEN verify phase executes
- THEN LLM response contains "yes" (case-insensitive substring)
- AND loop terminates with success

#### Scenario: PlanExecuteLoop plan phase generates executable DSL
- GIVEN real deepseek PlanExecuteLoop
- WHEN plan phase executes with prompt "compute 2+3"
- THEN LLM returns DSL with valid ll_call/tool_call nodes
- AND DSL is parseable (execute_phase succeeds)

### Requirement: CostTrackingDecorator 真实 token 计费 (P0)

`cost_tracking_decorator.cpp` 通过 usage→tokens 映射扣 budget (cloud_adapter.cpp:344-349)。真实 LLM 测试 SHALL 验证 token 数 > 0,budget 扣费准确。

#### Scenario: CostTrackingDecorator accurately charges tokens
- GIVEN real deepseek decorated with CostTrackingDecorator
- WHEN generate called with 100-token prompt
- THEN budget 扣费 = (prompt_tokens + completion_tokens) × per_token_cost
- AND completion_tokens > 0 (LLM actually responds)
- AND budget_remaining() decreases accordingly

### Requirement: SkillInterpreter IPC llm_generate (P1)

`skill_interpreter.cpp:659` 同步 generate + `skill_child_main.cpp:87-97` 逐字节 read。真实 LLM 测试 SHALL 验证 SKILL.md 含 `llm_generate(...)` 语句时,真实 LLM 通过 seccomp 白名单 + pipe IPC 返回结果。

#### Scenario: Skill llm_generate via IPC returns real LLM response
- GIVEN real deepseek SkillInterpreter
- AND SKILL.md with `llm_generate("Say hello")` statement
- WHEN SkillInterpreter.execute(skill_md)
- THEN child process invokes real deepseek
- AND result returned via pipe IPC
- AND skill execution completes within reasonable time

### Requirement: YieldNode 流式取消 (P1, deferred)

`node_executor.cpp:576` `generate_stream(req, {})` 硬编码空 token,**当前无法取消流式 LLM 生成**。本 change SHALL 记录此断裂点,等 token 透传 fix-up change 后再启用测试。

#### Scenario: YieldNode streaming cancellation GAP (deferred)
- GIVEN current production code (token = {})
- WHEN cancel propagated
- THEN token NOT observed by YieldNode → stream continues → cost+delay paid
- AND gap documented for fix-up change

### Requirement: GEPA 反射真实 LLM (P1)

`gepa_loop.cpp:116` 调 LLM 编译 skill 失败时 continue,优雅跳过。真实 LLM 测试 SHALL 验证 N 轮反射下系统稳定。

#### Scenario: GEPA N-round reflection graceful under real LLM
- GIVEN real deepseek GEPA loop
- AND skill that fails to compile (intentional)
- WHEN GEPA runs 5 rounds
- THEN each round invokes real LLM (verify time > 5s minimum)
- AND continue path works (no panic)

### Requirement: ContextCompactor 真实 LLM 摘要 (P1)

`context_compactor.cpp:64` 摘要调用 LLM,`count_tokens` 仍是 TODO。真实 LLM 测试 SHALL 验证摘要过程稳定,token 计数 fallback 合理。

#### Scenario: ContextCompactor summarization with real LLM
- GIVEN real deepseek ContextCompactor
- AND long conversation history (50 messages)
- WHEN compact called
- THEN LLM generates summary
- AND history size reduced
- AND approximate token count consistent with response

### Requirement: helper 迁移 (Foundation)

`chat-real-llm-coverage` 的 `examples/pdk_chat_demo/tests/test_helpers/real_llm_env.h` SHALL 提升到项目级 `tests/test_helpers/real_llm_env.h`,供 core 路径复用。

#### Scenario: Helper available at project-level path
- GIVEN helper at `tests/test_helpers/real_llm_env.h`
- WHEN core test `#include "test_helpers/real_llm_env.h"`
- THEN CMake target_include_directories resolves helper from root tests/

#### Scenario: Helper self-test at project-level
- GIVEN helper migrated
- WHEN `test_real_llm_env_helper.cpp` runs from root tests/ build
- THEN 4 cases PASS: skip silent / no key FAIL / deepseek config / minimax fallback

### Requirement: CI 默认 skip (前瞻)

`.github/workflows/ci.yml` SHALL 添加 `env:HYDRAFORGE_SKIP_REAL_LLM: "1"`:
- 当前 CI 默认 examples=OFF → core 测试树已运行 → 缺 key 会导致 core real LLM 测试 FAIL
- 必须添加 skip env 默认值保护 CI

#### Scenario: CI default skip prevents regression
- GIVEN `.github/workflows/ci.yml` env `HYDRAFORGE_SKIP_REAL_LLM: "1"`
- WHEN fork PR (无 secrets) runs CI
- THEN real LLM tests WARN + return (skip)
- AND CI passes

### Requirement: 测试基础设施无回归

所有 Phase A-G 新增测试 SHALL:
- 编译通过 (0 error, 0 warning) under debug preset
- 不引入 test regression (现有 223 ctest baseline 保持 PASS)
- 新增 test 总数 SHALL ≤ 25 cases (避免 ctest 执行时间爆炸)
- 真实 LLM 测试 SHALL 在 API key unset + skip flag unset 时 FAIL (硬门槛)
- 真实 LLM 测试 SHALL 在 API key set 时 PASS (满足内容契约)

#### Scenario: Full ctest baseline preserved after new tests
- GIVEN baseline 223 tests PASS (per chat-real-llm-coverage ship)
- WHEN all 7 phases (A-G) of this change ship
- THEN total ctest count = 223 + ≤25 new = ≤248
- AND baseline 223 tests remain PASS (no regression)

### Requirement: 范围边界 (Out of Scope)

以下场景**不在**本 change 范围, 另立 fix-up / 独立 changes SHALL 不被本 change 覆盖:

#### Scenario: Token passthrough fixes SHALL 不被本 change 覆盖
- 归属: `fix-yield-node-token-passthrough` / `fix-orchestrator-token-passthrough` / `fix-skill-interpreter-token-and-timeout`
- 理由: 涉及生产代码变更 (CancellationToken 透传),非 test-only
- 本 change 仅在测试中 `INFO()` 记录已知断裂点

#### Scenario: pdk_chat_demo 真实 LLM 测试 SHALL 不被本 change 覆盖
- 归属: `chat-real-llm-coverage` (sibling)
- 理由: 不同测试脚手架 (pdk_chat_demo fixture vs CognitiveWorker fixture),零重叠

#### Scenario: /model 运行时 provider 切换 SHALL 不被本 change 覆盖
- 归属: `chat-model-switch-real`
- 理由: 需生产代码变更 (chat() 入口消费 next_model_ + provider 重绑)