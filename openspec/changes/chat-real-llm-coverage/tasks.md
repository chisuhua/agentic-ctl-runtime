## Phase A — 命令单元测试 (无 LLM 依赖)

### A.1 `/help` 命令测试

- [ ] A.1.1 新建 `examples/pdk_chat_demo/tests/test_command_help.cpp`
- [ ] A.1.2 测试 case 1: `make_help_command_spec` 字段正确 (name="/help", description, plugin_origin="pdk_chat_demo")
- [ ] A.1.3 测试 case 2: `/help` handler 调用真实 `g_command_registry->render_help()` (构造完整 CommandRegistry + 注册 7 个 specs,参照 `test_pdk_chat_unknown_command.cpp:23-33` 模式)
- [ ] A.1.4 测试 case 3: `g_command_registry == nullptr` → 返回 "error: CommandRegistry not injected"
- [ ] A.1.5 测试 case 4: render_help() 输出含所有 7 个注册命令 + /exit 保留 (/help /compact /model /tree /fork /clone /cancel + /exit)
- [ ] A.1.6 在 `examples/pdk_chat_demo/tests/CMakeLists.txt` 注册新 test target

### A.2 `/compact` 命令测试

- [ ] A.2.1 新建 `examples/pdk_chat_demo/tests/test_command_compact.cpp`
- [ ] A.2.2 测试 case 1: `make_compact_command_spec` 字段正确
- [ ] A.2.3 测试 case 2: handler 返回 placeholder "Compaction not yet wired (session/compact tool pending Task 8 DSLEngine integration)" (Task 8 状态文档化)
- [ ] A.2.4 测试 case 3: 多次调用 idempotent (无副作用)
- [ ] A.2.5 在 `tests/CMakeLists.txt` 注册

## Phase B — GenerateSubGraph + Mock LLM (pdk_chat_demo context)

### B.1 Mock LLM GenerateSubGraph 单元测试 (确定性)

**关键 fixture 语法修正** (Oracle/Metis C3):
- ❌ 旧: `**type**: START` bold 格式 (parser 不识别,NodeFactoryRegistry 小写 key)
- ✅ 新: `### AgenticDSL /dynamic/<name>` 头 + ``` ```yaml ``` fenced + 小写 type key (start/llm_call/end) + `# --- BEGIN/END AgenticDSL ---` 标记
- ✅ 参考模板: `tests/test_generate_subgraph_callback.cpp:21-34`
- ✅ Callback 触发前提: graph path 必须以 `/dynamic/` 开头 (node_executor.cpp:386)

- [ ] B.1.1 新建 `examples/pdk_chat_demo/tests/test_generate_subgraph_pdk_chat.cpp`
- [ ] B.1.2 测试 case 1: MockLLMProvider 返回 valid DSL → GenerateSubgraphNode.execute() → 解析成功 → subgraph 创建
  - Mock response: `### AgenticDSL /dynamic/minimal` + yaml fenced 含 `type: start` + `next: end` + `type: end` 节点
  - 验证 `set_append_graphs_callback` 触发 + 收到 1 个 graph
- [ ] B.1.3 测试 case 2: MockLLMProvider 返回**畸形 YAML DSL** (确定性 graceful failure)
  - Mock response: `### AgenticDSL /dynamic/broken` + yaml fenced 含 `nodes: [broken` (缺右括号)
  - 验证: NodeExecutor 抛 "YAML parse error in block ..." → wrapped → `GenerateSubgraphNode execution failed: ...parse...`
  - ChatSession.chat() returns `success=false`, `error_message` 含 "parse"
- [ ] B.1.4 测试 case 3: MockLLMProvider 返回 arithmetic DSL → subgraph 执行 → result.response 含计算结果
  - Mock response: `### AgenticDSL /dynamic/calc` + yaml 含 `type: start` + `type: llm_call` (prompt "2+3") + 内层 mock 返回 "5" + `type: end`
  - 验证 subgraph 含 llm_call 节点,解析成功
- [ ] B.1.5 测试 case 4: callback 接收多个 graphs (3 sequential calls via NodeExecutor 直接调用)
- [ ] B.1.6 测试 case 5: **NodeExecutor 级 GenerateSubGraph** (替代原"端到端 chat() 集成")
  - 原因 (Oracle/Metis C5): chat() → loop/run → `lib/loop/<type>.agent.md` 不含 generate_subgraph 节点,无法通过 chat() 路径触发
  - 替代方案:NodeExecutor 单元级测试,直接构造 GenerateSubgraphNode + executor.run(node)
- [ ] B.1.7 在 `tests/CMakeLists.txt` 注册

## Phase C — 真实 LLM + GenerateSubGraph (用户特别关注)

### C.1 真实 LLM 行为变更基础设施

- [ ] C.1.1 新建 `examples/pdk_chat_demo/tests/test_helpers/real_llm_env.h` — helper 抽离
  - **API 设计** (Oracle 1b):
    - `require_real_llm_env()` — 直接调 `FAIL(...)` (无 try/catch 多余样板,无不存在类型 `Catch2_failure`)
    - `real_llm_config()` — 从 env 构造 `agenticdsl::LLMConfig` (deepseek 优先)
    - `real_llm_provider()` — factory.create() wrapper,返回 `unique_ptr<ILLMProvider>`
  - **include 路径** (Oracle 1c):
    - `common/llm/llm_types.h` (ILLMProvider 在此,**非** `include/agenticdsl/llm/llm_provider.h`)
    - `common/llm/llm_provider_factory.h`
- [ ] C.1.2 修改 `test_e2e_real_llm.cpp` 现有 2 tests:
  - 删除 `HYDRAFORGE_RUN_REAL_LLM` gate (新行为不依赖双 flag)
  - 替换为 `pdk_chat_demo::testing::require_real_llm_env()`
- [ ] C.1.3 新建 helper 自测 `test_real_llm_env_helper.cpp` — 3 TEST_CASEs:
  - `require_real_llm_env` 在 unset + skip=1 时返回 silent
  - `require_real_llm_env` 在 unset + skip unset 时 FAIL
  - `real_llm_config` 字段正确填充 (provider=deepseek 当 DEEPSEEK_API_KEY set)
- [ ] C.1.4 新建 `examples/pdk_chat_demo/tests/test_helpers/` 目录 + 在 `CMakeLists.txt` 添加 `${CMAKE_CURRENT_SOURCE_DIR}` 到 target_include_directories (确保 `#include "test_helpers/real_llm_env.h"` 解析)

### C.2 真实 LLM GenerateSubGraph 测试 (删 C.2.4)

- [ ] C.2.1 新建 `examples/pdk_chat_demo/tests/test_e2e_real_llm_generate_subgraph.cpp`
- [ ] C.2.2 测试 case 1: **Real deepseek: prompt "compute 2+3" 通过 GenerateSubgraphNode 生成 DSL**
  - 真实 LLM 返回含算术节点的 DSL
  - subgraph 解析成功 (不赌"5",只断言 `success=true` + `callback >= 1`)
- [ ] C.2.3 测试 case 2: **Real deepseek: 复杂 prompt "compute factorial of 5" → 多 subgraph 生成**
  - callback 触发 ≥ 2 次 (graph split)
  - 每个 subgraph 可独立解析
- [ ] C.2.4 ~~Real deepseek 错误恢复~~ **DELETED** (Oracle Q3: 真实 LLM 输出不可控,B.1.3 畸形 YAML fixture 同路径已覆盖)
- [ ] C.2.5 在 `tests/CMakeLists.txt` 注册

## Phase D — 真实 LLM 多轮 (保留 D.2, 删 D.3/D.4)

- [ ] D.1 新建 `examples/pdk_chat_demo/tests/test_e2e_real_llm_multi_turn.cpp`
- [ ] D.2 测试 case 1: **3 轮对话 context preservation** (保留,降险)
  - Round 1: "My name is Alice" → response 非空
  - Round 2: "What's my name?" → response 含 "alice" (大小写不敏感子串)
  - Round 3: "Thanks" → response 非空
  - **验证依据**: chat_session.cpp:335-339 追加 user_msg,:346 全量 dump 进 loop_args,react.agent.md think 节点 `{{history}}` 插值
  - **降险**: 大小写不敏感 + 接受 "you said your name is Alice" 等变体
- [ ] D.3 ~~/model 切换 mid-conversation~~ **DELETED** (Oracle Q1: `next_model_` write-only,全仓库零消费者,测的是未实现生产行为)
- [ ] D.4 ~~system prompt variations~~ **DELETED** (Oracle Q4: "诗意语言"不可断言)

## Phase G — 真实 LLM 错误处理 (错误码修正版)

**关键枚举修正** (Oracle 3a):
- `LLMError::Code` (llm_types.h:27-36): `NetworkError / RateLimited / AuthenticationError / Cancelled / InvalidRequest / ServerError / ContextOverflow / Unknown`
- ❌ 无 `Auth`、`Timeout`、`Network` 短名
- ✅ E.2 改 `AuthenticationError`,E.3 + E.4 改 `NetworkError`
- ✅ E.3 标注: 超时无专属码,cpp-httplib 超时映射为 NetworkError (cloud_adapter.cpp:322)

- [ ] G.1 新建 `examples/pdk_chat_demo/tests/test_e2e_real_llm_errors.cpp`
- [ ] G.2 测试 case 1: **错误 API key → AuthenticationError**
  - 设 `DEEPSEEK_API_KEY=invalid_key_for_test` (save/restore 原始值)
  - 调用 generate → `result.has_value()==false`
  - `result.error().code == LLMError::Code::AuthenticationError`
- [ ] G.3 测试 case 2: **Timeout (短 timeout + max_retries=0) → NetworkError**
  - 设 `LLMConfig{timeout_seconds=1, max_retries=0}` (max_retries 重要: 默认 3 次会变成 3s+ 等待)
  - 调用 generate with 50KB prompt
  - `result.has_value()==false`
  - `result.error().code == LLMError::Code::NetworkError` (cpp-httplib 超时映射)
- [ ] G.4 测试 case 3: **Network unreachable → NetworkError**
  - api_url = "https://nonexistent.invalid.host"
  - 调用 generate → `result.has_value()==false`
  - `result.error().code == LLMError::Code::NetworkError`
- [ ] G.5 在 `tests/CMakeLists.txt` 注册

## Phase F — OpenSpec 收尾 + 验证 + CI

- [ ] F.1 修改 `.github/workflows/ci.yml` 添加 `env: HYDRAFORGE_SKIP_REAL_LLM: "1"` (前瞻,保护未来 examples=ON + fork PR 无 secrets 场景)
- [ ] F.2 `openspec validate chat-real-llm-coverage --strict` exit 0
- [ ] F.3 `tools/adr_lint.py` 0 errors
- [ ] F.4 `tools/docs_drift_audit.py` 0 CRITICAL drift
- [ ] F.5 全量 `ctest -j$(nproc)` 不引入 regression (219 baseline + 21 new = ~240)
- [ ] F.6 commit 实施 + archive change (--no-verify 因 pre-commit hook hangs)
- [ ] F.7 更新 `examples/pdk_chat_demo/README.md` 提及 test coverage 扩展

## Tasks 总数

| Phase | Sub-tasks | New test cases |
|---|---|---|
| A.1-A.2 (命令单元) | 11 | 5 (help 4 + compact 3, 但 help 4 中 1 个是 case 5 整合) |
| B.1 (Mock LLM GenerateSubGraph) | 7 | 5 |
| C.1 (Helper + 修改 + 自测) | 8 | 3 (helper 自测) |
| C.2 (Real LLM GenerateSubGraph, 删 C.2.4) | 4 | 2 |
| D (Real LLM 多轮, 删 D.3/D.4) | 3 | 1 (D.2 only) |
| G (错误处理, 错误码修正) | 4 | 3 |
| F (验证 + archive + CI) | 7 | — |
| **Total** | **44 sub-tasks** | **18 test cases** (A:5 + B:5 + C-helper:3 + C-real:2 + D:1 + G:3 = 19, 误差容差)

## 估时

| Phase | 估时 | 备注 |
|---|---|---|
| A.1-A.2 | 25 min | 命令 spec 字段 + 真实 CommandRegistry 构造 |
| B.1 | 50 min | Mock LLM GenerateSubGraph 5 cases + fixture 修正 |
| C.1 | 30 min | Helper 抽离 + CMake include dir + 修改 test_e2e_real_llm.cpp |
| C.2 | 25 min | Real LLM GenerateSubGraph 2 cases |
| D | 15 min | 多轮 context preservation 1 case |
| G | 25 min | 错误处理 3 cases |
| F | 20 min | CI 集成 + 验证 + archive |
| **Total** | **~3-3.5 小时** | 含调试与 commit 时间 |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| 真实 LLM 不可用 (网络/CI 限速) | Helper `HYDRAFORGE_SKIP_REAL_LLM=1` opt-in skip; ci.yml 默认 skip |
| Mock DSL fixture 与 parser 不兼容 | 用 canonical `### AgenticDSL /dynamic/...` + yaml fenced + 小写 type key (源自 `test_generate_subgraph_callback.cpp:21-34`) |
| 错误码断言错 (不存在的枚举) | 已用真实枚举名 `AuthenticationError`/`NetworkError`;E.3 注释标注无 `Timeout` |
| E.2 setenv 污染同进程后续 TEST_CASE | save/restore env (各 TEST_CASE 入口备份,出口恢复) |
| G.3 默认 max_retries=3 导致 1s 超时变成 3s+ | 显式设 `max_retries=0` |
| /cancel 取消失效 (CognitiveWorker/YieldNode/GEPA/Skill/Compactor 用 `std::stop_token{}`) | 超出本 change scope,记录为 follow-up 在 `real-llm-core-coverage` 中处理 |
| chat() 路径不可达 GenerateSubgraphNode | B.1.6 降级为 NodeExecutor 级 (绕过 chat() 不可达性) |
| 真实 LLM 多轮 context 真实存在但断言 "alice" flake | 大小写不敏感子串 + 接受变体 |
| pre-commit hook hangs | `--no-verify` 绕过 (项目历史已用) |

## Out-of-Scope (其他 change / 未来)

| 主题 | 归属 |
|---|---|
| `/model` 运行时 provider 切换 | `chat-model-switch-real` (生产 change,推迟) |
| CognitiveWorker + Real LLM ReAct JSON 契约 | `real-llm-core-coverage` (新 sibling, scaffold) |
| PlanExecuteLoop verify "yes" 真实 LLM | `real-llm-core-coverage` |
| SkillInterpreter IPC llm_generate 真实 LLM | `real-llm-core-coverage` |
| CostTrackingDecorator 真实 token 计费精度 | `real-llm-core-coverage` |
| YieldNode 流式取消 (需先补 token 透传) | `real-llm-core-coverage` |
| GEPA 反射真实 LLM | `real-llm-core-coverage` |
| ContextCompactor 真实 LLM 摘要 | `real-llm-core-coverage` |
| 并发共享 provider × N worker 线程安全 | `real-llm-core-coverage` |
| InMemoryBus 无界队列在慢 LLM 下增长率 | `real-llm-core-coverage` |