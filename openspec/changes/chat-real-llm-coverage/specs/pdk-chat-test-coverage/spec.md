# Spec: pdk-chat-test-coverage

## Purpose

为 `pdk_chat_demo` 提供完整的测试覆盖矩阵: 真实 LLM 端到端 + 所有 chat 命令 + GenerateSubGraph (LLM 生成计算图)。

## ADDED Requirements

### Requirement: /help 命令路径覆盖

`pdk_chat_demo::make_help_command_spec` 的测试覆盖 SHALL 验证:
- name 字段 = "/help"
- description 字段非空
- plugin_origin 字段 = "pdk_chat_demo"
- handler 在 `g_command_registry != nullptr` 时返回 `render_help()` 输出
- handler 在 `g_command_registry == nullptr` 时返回 "error: CommandRegistry not injected"
- render_help 输出 SHALL 含 7 个注册命令 (`/help` `/compact` `/model` `/tree` `/fork` `/clone` `/cancel`) + `/exit` 保留字 (command_registry.cpp:60-61)

#### Scenario: /help with valid CommandRegistry
- GIVEN CommandRegistry with 7 specs registered
- WHEN `/help` handler invoked
- THEN output contains all 7 command names + /exit reserved word

#### Scenario: /help with null CommandRegistry
- GIVEN `g_command_registry = nullptr`
- WHEN `/help` handler invoked
- THEN output = "error: CommandRegistry not injected"

### Requirement: /compact 命令路径覆盖

`pdk_chat_demo::make_compact_command_spec` 的测试覆盖 SHALL 验证:
- name 字段 = "/compact"
- handler 返回 placeholder 消息表明 Task 8 未完成 (Task 8 = DSLEngine session/compact 集成)
- handler 多次调用 SHALL idempotent (无副作用)

#### Scenario: /compact placeholder behavior
- WHEN `/compact` handler invoked
- THEN output contains "Compaction not yet wired" or similar Task 8 marker

### Requirement: GenerateSubGraph + Mock LLM 覆盖 (确定性)

`GenerateSubgraphNode` + `NodeExecutor::execute_generate_subgraph` 的测试覆盖 SHALL 验证:
- MockLLMProvider 返回 valid DSL (`### AgenticDSL /dynamic/minimal` + yaml fenced + 小写 type) → subgraph 解析 + 创建 + `set_append_graphs_callback` 触发
- MockLLMProvider 返回**畸形 YAML DSL** (`### AgenticDSL /dynamic/broken` + `nodes: [broken`) → graceful failure (success=false, error_message 含 "parse")
- MockLLMProvider 返回 arithmetic DSL → subgraph 含 `type: llm_call` 节点,可独立解析
- callback 接收 multiple graphs (3 sequential calls via NodeExecutor 直接调用)
- NodeExecutor 级 GenerateSubGraph 测试 (替代 chat() 端到端路径不可达)

#### Scenario: Mock LLM returns valid DSL → subgraph executes
- GIVEN MockLLMProvider enqueued with canonical minimal DSL (`### AgenticDSL /dynamic/minimal` + yaml + `type: start`/`type: end`)
- WHEN GenerateSubgraphNode executes
- THEN set_append_graphs_callback receives 1 graph
- AND execution completes without error

#### Scenario: Mock LLM returns malformed YAML → graceful failure (确定性)
- GIVEN MockLLMProvider enqueued with malformed YAML DSL (`### AgenticDSL /dynamic/broken` + `nodes: [broken` 缺右括号)
- WHEN GenerateSubgraphNode executes
- THEN NodeExecutor throws std::runtime_error containing "YAML parse error"
- AND wrapped as "GenerateSubgraphNode execution failed: ...parse..." (node_executor.cpp:101-102)
- AND no crash (process continues)
- AND error_message contains "parse"

### Requirement: 真实 LLM 端到端测试覆盖 (WARN → FAIL 行为变更)

`test_e2e_real_llm.cpp` 现有 2 tests + 所有新增真实 LLM 测试 SHALL 遵循以下 env var 真值表:

| `HYDRAFORGE_SKIP_REAL_LLM` | API key 存在? | 行为 |
|---|---|---|
| `=1` (任意大小写) | 任意 | **WARN + return** (opt-in skip) |
| 未设 / `=0` / 其他 | 是 (DEEPSEEK or MINIMAX) | **run** (真实 LLM 调用) |
| 未设 / `=0` / 其他 | 否 | **FAIL** (硬失败) |

helper SHALL 抽离到 `examples/pdk_chat_demo/tests/test_helpers/real_llm_env.h`:
- `require_real_llm_env()` — 直接调 Catch2 `FAIL(...)` 宏 (无 try/catch 多余样板,无不存在类型)
- `real_llm_config()` — 从 env 构造 `LLMConfig` (deepseek 优先)
- `real_llm_provider()` — factory.create wrapper,返回 `unique_ptr<ILLMProvider>`

#### Scenario: API key unset, no skip flag → test FAILS
- GIVEN no `DEEPSEEK_API_KEY` / `MINIMAX_API_KEY` set
- GIVEN `HYDRAFORGE_SKIP_REAL_LLM` unset or != "1"
- WHEN real LLM test runs
- THEN helper calls `FAIL("real LLM env required: set DEEPSEEK_API_KEY ...")`
- AND execution halts at TEST_CASE entry (not silently skipped)

#### Scenario: API key unset, skip flag set → test SKIPS
- GIVEN no `DEEPSEEK_API_KEY` / `MINIMAX_API_KEY` set
- GIVEN `HYDRAFORGE_SKIP_REAL_LLM=1`
- WHEN real LLM test runs
- THEN helper returns silently
- AND test proceeds but no LLM call made (test design SHALL short-circuit after helper)

#### Scenario: DEEPSEEK_API_KEY set → real deepseek call
- GIVEN `DEEPSEEK_API_KEY=sk-...`
- WHEN real LLM test runs
- THEN `real_llm_config()` returns provider="deepseek", env_used="DEEPSEEK_API_KEY"
- AND `real_llm_provider()` constructs real deepseek client

#### Scenario: MINIMAX_API_KEY set, DEEPSEEK unset → real minimax call
- GIVEN `DEEPSEEK_API_KEY` unset
- GIVEN `MINIMAX_API_KEY=...`
- WHEN real LLM test runs
- THEN `real_llm_config()` returns provider="minimax", env_used="MINIMAX_API_KEY"

#### Scenario: Both keys set → DEEPSEEK priority
- GIVEN `DEEPSEEK_API_KEY` AND `MINIMAX_API_KEY` both set
- WHEN real LLM test runs
- THEN `real_llm_config()` returns provider="deepseek" (priority order)

#### Scenario: Helper exposed at pdk_chat_demo-specific path
- GIVEN helper at `examples/pdk_chat_demo/tests/test_helpers/real_llm_env.h`
- WHEN test file `#include "test_helpers/real_llm_env.h"`
- THEN CMake target_include_directories contains `${CMAKE_CURRENT_SOURCE_DIR}` (resolution path)

### Requirement: Helper 自测覆盖

`test_real_llm_env_helper.cpp` SHALL 验证 helper 自身契约:
- `require_real_llm_env()` 在 unset key + skip=1 时静默返回
- `require_real_llm_env()` 在 unset key + skip unset 时 FAIL
- `real_llm_config()` 字段正确填充 (provider/model/api_url/api_endpoint/api_key/env_used)

#### Scenario: Helper self-test - skip flag silence
- GIVEN no API key, `HYDRAFORGE_SKIP_REAL_LLM=1`
- WHEN `require_real_llm_env()` called
- THEN returns silently (no FAIL)

#### Scenario: Helper self-test - no key no skip → FAIL
- GIVEN no API key, no skip flag
- WHEN `require_real_llm_env()` called
- THEN Catch2 marks current TEST_CASE as FAIL

#### Scenario: Helper self-test - config field population
- GIVEN `DEEPSEEK_API_KEY=test_key`
- WHEN `real_llm_config()` called
- THEN `config.provider == "deepseek"`
- AND `config.env_used == "DEEPSEEK_API_KEY"`
- AND `config.api_key == "test_key"`

### Requirement: 真实 LLM + GenerateSubGraph 覆盖 (用户特别关注)

`test_e2e_real_llm_generate_subgraph.cpp` SHALL 验证:
- Real deepseek prompt "compute 2+3" → 返回含算术节点的 DSL → subgraph 解析成功 (断言: success=true + callback ≥ 1,不赌 "5" 输出)
- Real deepseek 复杂 prompt "compute factorial of 5" → callback 触发 ≥ 2 次 (multi subgraph)
- **不测** Real LLM 错误恢复路径 (B.1.3 畸形 YAML 已覆盖确定性 graceful failure)

#### Scenario: Real LLM generates arithmetic subgraph
- GIVEN real deepseek provider configured
- WHEN prompt "compute 2+3" via GenerateSubgraphNode
- THEN subgraph parsed successfully
- AND `set_append_graphs_callback` receives ≥ 1 graph
- AND callback graph contains at least one `type: llm_call` node

#### Scenario: Real LLM triggers multiple subgraph callbacks
- GIVEN real deepseek provider
- WHEN complex prompt "compute factorial of 5" via GenerateSubgraphNode
- THEN set_append_graphs_callback fires ≥ 2 times
- AND each subgraph is independently parsable

### Requirement: 真实 LLM 多轮 context preservation 覆盖

`test_e2e_real_llm_multi_turn.cpp` SHALL 验证:
- 3 轮对话 context preservation (Round 1 自我介绍 → Round 2 验证记忆 → Round 3 引用前文)
- 断言用大小写不敏感子串 (接受 "you said your name is Alice" 等变体)
- **不测** /model mid-conversation 切换 (写-only 字段, 全仓库零消费者, 测的是未实现生产行为)
- **不测** system prompt 风格对比 (不可机器断言)

管道真实存在 (chat_session.cpp:335-339 追加 user_msg, :346 全量 dump 进 loop_args, react.agent.md think 节点 `{{history}}` 插值)。

#### Scenario: 3-round context preservation (case-insensitive)
- GIVEN real deepseek ChatSession
- WHEN Round 1: "My name is Alice" → response non-empty
- AND Round 2: "What's my name?" → response non-empty
- THEN Round 2 response contains "alice" (case-insensitive substring match, accepts "you said your name is Alice")
- AND Round 3: "Thanks" → response non-empty

### Requirement: 真实 LLM 错误处理覆盖 (错误码修正版)

`test_e2e_real_llm_errors.cpp` SHALL 验证:
- 错误 API key → `result.error().code == LLMError::Code::AuthenticationError` (llm_types.h:27-36 真实枚举,非不存在的 `Code::Auth`)
- Timeout (短 timeout + max_retries=0) → `result.error().code == LLMError::Code::NetworkError` (cpp-httplib 超时走 NetworkError,无专属 `Code::Timeout`)
- 无效 URL → `result.error().code == LLMError::Code::NetworkError` (同样真实枚举,非不存在的 `Code::Network`)

#### Scenario: Auth error with invalid key → AuthenticationError
- GIVEN `DEEPSEEK_API_KEY=invalid_key_for_test` (save/restore 原始值)
- WHEN generate called
- THEN result.has_value() == false
- AND result.error().code == LLMError::Code::AuthenticationError

#### Scenario: Timeout (short timeout + max_retries=0) → NetworkError
- GIVEN LLMConfig{timeout_seconds=1, max_retries=0} (max_retries 重要: 默认 3 次会变成 3s+ 等待)
- WHEN generate called with 50KB prompt
- THEN result.has_value() == false
- AND result.error().code == LLMError::Code::NetworkError
- AND elapsed time < 5s (verify max_retries=0 effective)

#### Scenario: Network error with invalid host → NetworkError
- GIVEN LLMConfig{api_url="https://nonexistent.invalid.host"}
- WHEN generate called
- THEN result.has_value() == false
- AND result.error().code == LLMError::Code::NetworkError

### Requirement: CI 集成 (前瞻)

`.github/workflows/ci.yml` SHALL 添加 `env:HYDRAFORGE_SKIP_REAL_LLM: "1"`:
- 当前 CI 不构建 examples (`AGENTICDSL_BUILD_EXAMPLES=OFF`),所以现状无影响
- 前瞻: 未来启用 examples 时,默认 skip 保护 fork PR (无 secrets) 场景
- 真实 LLM 验证留 `workflow_dispatch` 手动 job

#### Scenario: CI default skip ensures fork PR safety
- GIVEN `.github/workflows/ci.yml` env `HYDRAFORGE_SKIP_REAL_LLM: "1"`
- WHEN fork PR (无 DEEPSEEK/MINIMAX secrets) runs CI with examples=ON
- THEN real LLM tests WARN + return (skip)
- AND CI passes (no FAIL due to missing key)

### Requirement: 测试基础设施无回归

所有 Phase A/B/C/D/G 新增测试 SHALL:
- 编译通过 (0 error, 0 warning) under debug preset
- 不引入 test regression (现有 219 ctest baseline 保持 PASS)
- 新增 test 总数 SHALL ≤ 25 cases (避免 ctest 执行时间爆炸;从 ≤20 放宽,因删除 D.3/D.4/C.2.4 后仍有 18 + helper 自测 3 = ~21 cases)
- 真实 LLM 测试 SHALL 在 API key unset + skip flag unset 时 FAIL (硬门槛)
- 真实 LLM 测试 SHALL 在 API key set 时 PASS (满足内容契约)

#### Scenario: Full ctest baseline preserved after new tests
- GIVEN baseline 219 tests PASS (per commit ec2a2c4)
- WHEN all 7 phases (A/B/C-helper/C-real/D/G/F) of this change ship
- THEN total ctest count = 219 + ~21 new = ≤240
- AND baseline 219 tests remain PASS (no regression)
- AND new tests compile clean (0 error, 0 warning)

### Requirement: 范围边界 (Out of Scope)

以下场景**不在**本 change 范围, 另立 sibling changes SHALL 不被本 change 覆盖:

#### Scenario: /model 运行时 provider 切换
- 归属: `chat-model-switch-real` (生产 change,推迟)
- 理由: `next_model_` write-only (chat_session.cpp:292-301), 全仓库零消费者
- 本 change 不实现 chat() 入口消费 + provider 重绑

#### Scenario: CognitiveWorker + Real LLM ReAct JSON 契约
- 归属: `real-llm-core-coverage` (新 sibling,作为未来路标 scaffold)
- 理由: 不同脚手架 (CognitiveWorker fixture vs ChatSession),与 pdk_chat_demo 测试零重叠
- 本 change 不覆盖 simple_orchestrator.cpp:118 的真实 LLM 调用

#### Scenario: SkillInterpreter IPC llm_generate 真实 LLM
- 归属: `real-llm-core-coverage`
- 理由: skill IPC 独立路径 (skill_child_main.cpp:87-97 + skill_interpreter.cpp:659),超本 change scope

#### Scenario: PlanExecuteLoop verify "yes" 真实 LLM
- 归属: `real-llm-core-coverage`
- 理由: core 测试树路径,不经 loop_agent

#### Scenario: CostTrackingDecorator 真实 token 计费
- 归属: `real-llm-core-coverage`
- 理由: 同上

#### Scenario: YieldNode 流式取消 + GEPA + ContextCompactor + 并发共享 provider
- 归属: `real-llm-core-coverage` + 各自的 fix-up change
- 理由: 各自需要 token 透传或生产改动 (YieldNode 当前 `std::stop_token{}`)

#### Scenario: InMemoryBus 无界队列在慢 LLM 下增长率
- 归属: `real-llm-core-coverage`
- 理由: 需 N worker × 真实 LLM 并发场景,与本 change 测试 fixture 隔离