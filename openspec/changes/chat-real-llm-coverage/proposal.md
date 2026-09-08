## Why

`pdk_chat_demo` 现有测试覆盖存在三个**关键 gap**:

1. **真实 LLM 端到端测试不足** — `test_e2e_real_llm.cpp` 仅 2 tests (单 prompt + 单轮 ChatSession), 缺失:
   - 多轮对话 (context preservation)
   - LLM 错误处理 (auth / network / timeout-via-network)

2. **命令路径覆盖不全** — 8 个命令注册 (7 + /exit 保留), 但 `/help` 和 `/compact` **零专用测试**

3. **GenerateSubGraph + 真实 LLM 完全没有测试**:
   - 现有 3 个 GenerateSubGraph 测试 (`tests/test_generate_subgraph_callback.cpp`) 仅用 MockLLMProvider
   - **无任何真实 LLM GenerateSubGraph 测试**
   - pdk_chat_demo 上下文内**零** GenerateSubGraph 测试

**额外行为变更要求** (用户显式要求):
- 当前 `test_e2e_real_llm.cpp` 缺 API key 时**WARN + skip** — 用户要求改为 **FAIL**
- 必须能从环境变量 `DEEPSEEK_API_KEY` / `MINIMAX_API_KEY` 读取
- 增加 `HYDRAFORGE_SKIP_REAL_LLM=1` 逃生口 (无 key 环境下显式 opt-in skip)

**Oracle/Metis 审查后 Scope 修正** (基于真实代码事实):
- ❌ **删除 D.3** (`/model` 切换 mid-conversation) — `next_model_` 是 write-only (chat_session.cpp:292-301), 全仓库零消费者,测的是未实现的生产行为。Non-goals 禁止改 production,故另立独立生产 change (`chat-model-switch-real`)
- ❌ **删除 C.2.4** (Real LLM 错误恢复) — 真实 LLM 输出不可控,且纯文本 "hello world" parse 不抛异常反向失败。B.1.3 用畸形 YAML fixture 覆盖同路径
- ❌ **删除 D.4** (system prompt 风格对比) — "诗意语言" 不可机器断言,深覆盖已够
- ✅ **修正 E 错误码** — `Auth` → `AuthenticationError`;`Network` → `NetworkError`;`Timeout` 无对应枚举→改断言 `NetworkError` 并标注(超时走 cpp-httplib→NetworkError)
- ✅ **修正 B.1.3 fixture** — 改用畸形 YAML (`### AgenticDSL /dynamic/x` + `nodes: [broken`),确定性触发 "YAML parse error in block"
- ✅ **修正 D.2** — 保留(管道真实存在),断言改大小写不敏感 "alice" 子串

## What Changes

### 新增测试

**Phase A — 命令单元测试** (无需 LLM):
- `test_command_help.cpp` — `/help` 注册命令列表 + null registry 错误处理
- `test_command_compact.cpp` — `/compact` placeholder 行为验证

**Phase B — GenerateSubGraph + Mock LLM** (pdk_chat_demo context, 确定性):
- `test_generate_subgraph_pdk_chat.cpp` — 5 cases:
  - Mock LLM 返回 valid DSL → GenerateSubgraphNode → parser → execution
  - Mock LLM 返回 malformed YAML → graceful failure (success=false, error_message 含 "parse")
  - Mock LLM 返回 arithmetic DSL → subgraph 执行 → result.response 含计算结果
  - callback 多 graph 追加
  - NodeExecutor 级 GenerateSubGraph (端到端 chat() 路径当前不可达,见 C5 Reachability Gap)

**Phase C — 真实 LLM + GenerateSubGraph** (用户特别关注):
- `test_e2e_real_llm_generate_subgraph.cpp` — 3 cases:
  - Real deepseek: prompt "compute 2+3" → 生成算术 DSL → subgraph 解析 + 执行成功
  - Real deepseek: 复杂 prompt → callback 触发多个 subgraph
  - Real deepseek: 验证 prompt template 渲染与 LLM_CALL 节点出现

**Phase D — 真实 LLM 多轮** (保留 D.2, 删 D.3/D.4):
- `test_e2e_real_llm_multi_turn.cpp` — 2 cases:
  - 3 轮对话 context preservation (大小写不敏感 "alice" 子串)

**Phase E — 真实 LLM 错误处理** (错误码修正版):
- `test_e2e_real_llm_errors.cpp` — 3 cases:
  - 错误 API key → `LLMError::Code::AuthenticationError`
  - Timeout (short timeout, max_retries=0) → `LLMError::Code::NetworkError` (cpp-httplib 超时映射)
  - Network unreachable → `LLMError::Code::NetworkError`

### 行为变更 (BC) — WARN → FAIL 真值表

**env var 优先级** (明确定义):
| `HYDRAFORGE_SKIP_REAL_LLM` | API key 存在? | 行为 |
|---|---|---|
| `=1` (任意大小写) | 任意 | **WARN + return** (opt-in skip) |
| 未设 / `=0` / 其他 | 是 (DEEPSEEK or MINIMAX) | **run** (真实 LLM 调用) |
| 未设 / `=0` / 其他 | 否 | **FAIL** (硬失败) |

**Before** (`test_e2e_real_llm.cpp:69-74`):
```cpp
const char* api_key = std::getenv("DEEPSEEK_API_KEY");
if (!api_key || api_key[0] == '\0') {
    WARN("DEEPSEEK_API_KEY unset — skipping");
    return;
}
if (std::getenv("HYDRAFORGE_RUN_REAL_LLM") == nullptr) {
    WARN("set HYDRAFORGE_RUN_REAL_LLM=1 to enable");
    return;
}
```

**After**:
```cpp
pdk_chat_demo::testing::require_real_llm_env();  // 直接 FAIL 或静默 skip
const char* api_key = std::getenv("DEEPSEEK_API_KEY");  // 已保证非空
```

**关键 BC 变化**:
- ❌ 移除 `HYDRAFORGE_RUN_REAL_LLM` gate (新行为不依赖双 flag)
- ✅ 单一 skip 机制 `HYDRAFORGE_SKIP_REAL_LLM=1`
- ✅ 真值表 3 行清晰

### 新增 Capabilities

- `pdk-chat-test-coverage`: 完整测试覆盖矩阵 (real LLM + commands + GenerateSubGraph)

### 修改 Capabilities

无 — 仅测试增补, 不修改生产代码 API。

## Impact

**受影响的代码**:
- `examples/pdk_chat_demo/tests/test_e2e_real_llm.cpp` — 行为变更 (WARN → FAIL + 移除 RUN_REAL_LLM gate)
- 新增 7 个 test 文件 (A-E phases) + 1 个 helper 头文件
- 新建目录: `examples/pdk_chat_demo/tests/test_helpers/`

**测试影响**:
- 新增 ctest test 数量:
  - Phase A: 5 cases (help + compact 命令单元)
  - Phase B: 5 cases (GenerateSubGraph + Mock LLM)
  - Phase C-helper: 3 cases (helper 自测)
  - Phase C-real: 3 cases (real LLM GenerateSubGraph)
  - Phase D: 2 cases (multi-turn context)
  - Phase E: 3 cases (错误处理)
  - **Total new: 21 cases**
- ctest 总数: 219 → ~240
- **新增上限**: spec cap 从 ≤20 放宽到 ≤25 (理由: 删除 D.3/D.4/C.2.4 后仍有 21 cases 含 helper 自测)

**CI 影响**:
- ✅ 当前 CI 不构建 examples (`AGENTICDSL_BUILD_EXAMPLES=OFF` in CMakeLists.txt:209) → 现有 6 matrix jobs 不受影响
- 🔧 **前瞻**: 在 `.github/workflows/ci.yml` 添加 `env: HYDRAFORGE_SKIP_REAL_LLM: "1"`,当未来启用 examples 时强制 skip,保护 fork PR 无 secrets 场景
- 🔧 手动真实验证留 `workflow_dispatch` job

**Non-goals** (明确范围边界):
- ❌ 不修改 production 代码 (`chat_session.cpp`, `main.cpp`, etc.)
- ❌ 不新增 LLM provider 实现
- ❌ 不修改 GenerateSubgraphNode 实现
- ❌ 不引入新的 mocking library
- ❌ 不实现 `/model` 运行时切换 (另立 `chat-model-switch-real` change)
- ❌ 不为 CognitiveWorker / DomainWorkerPool / SkillIPC / PlanExecuteLoop 添加 real LLM 测试 (另立 `real-llm-core-coverage` change,作为未来路标)

## 验证标准

- `cmake --build build --target <each new test> -j$(nproc)` 编译通过 (0 error, 0 warning)
- `ctest -R "test_command_help|test_command_compact|test_generate_subgraph_pdk_chat|test_real_llm_env|test_e2e_real_llm" --output-on-failure` 全绿 (有 API key 时)
- 无 API key 时 `test_e2e_real_llm*` → **FAIL** (非 skip)
- `HYDRAFORGE_SKIP_REAL_LLM=1` 时 → opt-in skip
- 全量 `ctest -j$(nproc) --output-on-failure` 不引入 regression (现有 219 baseline 保持 PASS)
- `openspec validate chat-real-llm-coverage --strict` exit 0
- `tools/adr_lint.py` 0 errors
- `tools/docs_drift_audit.py` 0 CRITICAL drift