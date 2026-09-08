# Design — chat-real-llm-coverage

## 测试架构 (3 层)

### Layer 1 — 命令单元测试 (Phase A)
- **位置**: `examples/pdk_chat_demo/tests/test_command_*.cpp`
- **依赖**: 纯 C++ (无 LLM, 无网络)
- **Mock**: 真实 `CommandRegistry` + 注册 7 specs (参照 `test_pdk_chat_unknown_command.cpp:23-33` 模式;`render_help()` 非虚,**无法 mock**)
- **运行时间**: < 100ms per test

### Layer 2 — GenerateSubGraph + Mock LLM (Phase B)
- **位置**: `examples/pdk_chat_demo/tests/test_generate_subgraph_pdk_chat.cpp`
- **依赖**: `MockLLMProvider` + `DSLEngine::from_markdown` + `NodeExecutor`
- **Mock**: `MockLLMProvider::enqueue_response(确定性 DSL 字符串)`
- **运行时间**: < 500ms per test

### Layer 3 — 真实 LLM (Phase C-real / D / G)
- **位置**: `examples/pdk_chat_demo/tests/test_e2e_real_llm_*.cpp`
- **依赖**: `DEEPSEEK_API_KEY` / `MINIMAX_API_KEY` env var
- **Skip**: `HYDRAFORGE_SKIP_REAL_LLM=1` opt-in (见真值表)
- **运行时间**: 5-30s per test (受网络/速率限制影响)

## Helper 抽离设计 (`examples/pdk_chat_demo/tests/test_helpers/real_llm_env.h`)

**路径** (统一): `examples/pdk_chat_demo/tests/test_helpers/real_llm_env.h` (项目级 `tests/test_helpers/` 不放)

**API** (Oracle 1b 修正版):

```cpp
// examples/pdk_chat_demo/tests/test_helpers/real_llm_env.h
#pragma once

#include <memory>
#include <string>
#include <cstdlib>

#include "common/llm/llm_types.h"           // ILLMProvider
#include "common/llm/llm_provider_factory.h" // LLMProviderFactory

namespace pdk_chat_demo::testing {

// 单一职责:helper 直接调 Catch2 FAIL — 无 try/catch 多余样板
// 用户调用形式: pdk_chat_demo::testing::require_real_llm_env();
// 失败语义: Catch2 FAIL 抛异常, Catch2 捕获并标记当前 TEST_CASE 为 FAIL
inline void require_real_llm_env() {
    // 1. opt-in skip
    if (const char* skip = std::getenv("HYDRAFORGE_SKIP_REAL_LLM");
        skip && std::string(skip) == "1") {
        return;  // 静默 skip
    }
    // 2. key set → ok
    if (const char* ds = std::getenv("DEEPSEEK_API_KEY");
        ds && ds[0] != '\0') return;
    if (const char* mm = std::getenv("MINIMAX_API_KEY");
        mm && mm[0] != '\0') return;
    // 3. 无 key → FAIL (硬失败)
    FAIL("real LLM env required: set DEEPSEEK_API_KEY or MINIMAX_API_KEY, "
         "or set HYDRAFORGE_SKIP_REAL_LLM=1 to opt-in skip");
}

struct RealLLMConfig {
    std::string provider;       // "deepseek" | "minimax"
    std::string model;          // e.g. "deepseek-v4-flash"
    std::string api_url;        // provider endpoint
    std::string api_endpoint;   // path
    std::string api_key;        // 取自 env (非 log)
    std::string env_used;       // "DEEPSEEK_API_KEY" | "MINIMAX_API_KEY"
};

// 从 env 构造配置 (deepseek 优先, fallback minimax)
RealLLMConfig real_llm_config();

// 构造 LLMProvider 实例 (默认 mock mode + DeepSeek config)
std::unique_ptr<ILLMProvider> real_llm_provider();

}  // namespace pdk_chat_demo::testing
```

**注意**:
- ✅ 无不存在类型 `Catch2_failure` (原 design 错误)
- ✅ 无 try/catch + FAIL 双层 (Catch2 FAIL 本身抛异常)
- ✅ `ILLMProvider` 来自 `common/llm/llm_types.h:112` (**非** `include/agenticdsl/llm/llm_provider.h`)
- ✅ Header-only inline (与 `tests/test_helpers/http_mock_server.h` 一致)

## 行为变更 (BC) — WARN → FAIL 真值表

**env var 优先级** (明确定义):

| `HYDRAFORGE_SKIP_REAL_LLM` | API key 存在? | 行为 |
|---|---|---|
| `=1` | 任意 | **WARN + return** (opt-in skip) |
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

**After** (简洁):
```cpp
// 测试 case 开头直接调 (FAIL 即终止, 否则继续)
pdk_chat_demo::testing::require_real_llm_env();
const char* api_key = std::getenv("DEEPSEEK_API_KEY");  // 已保证非空
```

**关键 BC 变化**:
- ❌ 移除 `HYDRAFORGE_RUN_REAL_LLM` gate (新行为不依赖双 flag,真值表 3 行清晰)
- ✅ 单一 skip 机制 `HYDRAFORGE_SKIP_REAL_LLM=1`
- ⚠️ `HYDRAFORGE_RUN_REAL_LLM=1` 旧设置保留兼容 — 仍可工作 (helper 不读它),但已不再是必要条件

## 测试文件结构

```
examples/pdk_chat_demo/
├── tests/
│   ├── test_command_help.cpp               [NEW Phase A.1]
│   ├── test_command_compact.cpp            [NEW Phase A.2]
│   ├── test_generate_subgraph_pdk_chat.cpp [NEW Phase B]
│   ├── test_e2e_real_llm.cpp               [MODIFY: WARN→FAIL + 移除 RUN_REAL_LLM gate]
│   ├── test_e2e_real_llm_generate_subgraph.cpp [NEW Phase C-real]
│   ├── test_e2e_real_llm_multi_turn.cpp    [NEW Phase D]
│   ├── test_e2e_real_llm_errors.cpp        [NEW Phase G]
│   ├── test_real_llm_env_helper.cpp        [NEW Phase C.1 helper 自测]
│   ├── test_helpers/                       [NEW 目录]
│   │   └── real_llm_env.h                  [NEW Phase C.1]
│   └── CMakeLists.txt                      [MODIFY: register all new targets + include dir]
└── README.md                                [MODIFY Phase F.7]
```

## Mock LLM DSL fixture 样本 (Phase B, 修正版)

**关键修正** (Oracle/Metis C3):
- ❌ 旧 `**type**: START` bold 格式 → parser 不识别
- ✅ 新 canonical 格式 (源自 `tests/test_generate_subgraph_callback.cpp:21-34`):

```cpp
// B.1.2: minimal DSL (start → end) — 用于 valid 路径
const std::string kMinimalDSL = R"(### AgenticDSL /dynamic/minimal
```yaml
# --- BEGIN AgenticDSL ---
type: start
name: start_node
next: end_node
---
type: end
name: end_node
# --- END AgenticDSL ---
```
)";

// B.1.4: arithmetic DSL (start → llm_call → end)
const std::string kArithmeticDSL = R"(### AgenticDSL /dynamic/calc
```yaml
# --- BEGIN AgenticDSL ---
type: start
name: start_node
next: calc_node
---
type: llm_call
name: calc_node
prompt: "2+3"
next: end_node
---
type: end
name: end_node
# --- END AgenticDSL ---
```
)";

// B.1.3: malformed YAML (确定性失败路径)
const std::string kMalformedDSL = R"(### AgenticDSL /dynamic/broken
```yaml
# --- BEGIN AgenticDSL ---
type: start
name: start_node
next: end_node
nodes: [broken
# --- END AgenticDSL ---
```
)";
```

**关键约束**:
- ✅ graph path 必须 `/dynamic/` 开头 (node_executor.cpp:386 callback 触发前提)
- ✅ 节点 type 小写 (`start` / `end` / `llm_call` — node_factory.cpp:339-347 注册 key)
- ✅ fenced 块必须 ```yaml (markdown_parser.cpp:301,310-314 只识别此格式)

Mock response 注册:
```cpp
auto mock = dynamic_cast<MockLLMProvider*>(provider.get());
mock->enqueue_response(kMinimalDSL);
mock->enqueue_response(kArithmeticDSL);
mock->enqueue_response(kMalformedDSL);
```

## GenerateSubGraph 错误恢复设计 (B.1.3, 确定性格式)

**Mock 路径** (替代原真实 LLM C.2.4 路径):

1. Mock 返回**畸形 DSL** (`kMalformedDSL` — `nodes: [broken` 缺右括号)
2. `extract_pathed_blocks` 找到 `### AgenticDSL /dynamic/broken` 头 → 返回该 block
3. `parse_from_string` 调用 yaml 解析 → **抛 "YAML parse error in block ..."**
4. `NodeExecutor::execute_generate_subgraph` 包装为 **`GenerateSubgraphNode execution failed: ...parse...`** (node_executor.cpp:101-102)
5. 测试验证:
   ```cpp
   REQUIRE(result.has_value() == false);  // 或等价 success = false
   REQUIRE(result.error.message.find("parse") != std::string::npos);
   ```

**Why mock-only 足够**:
- 确定性 (无 LLM 非确定性)
- 同一代码路径 (parse error → wrap → chat() catch → success=false)
- 真实 LLM 不可控 (Q3 决策: 删除 C.2.4)

## GenerateSubGraph 端到端 vs NodeExecutor 级 (B.1.6 决策)

**原计划**: ChatSession.chat() 端到端调用 GenerateSubGraph
**Oracle/Metis C5 Reachability Gap**:
- chat() → loop/run → 加载 `lib/loop/<type>.agent.md` (pdk_entry.cpp:81-82)
- 三个 loop 文件 (react/plan_execute/fork_join) **均不含 generate_subgraph 节点** (grep 确认)
- `execute_generate_subgraph` 要求 `ctx["__rendered_prompt__"]` (node_executor.cpp:342),常规 loop 流程谁渲染未定义

**修正方案** (B.1.6):
- ❌ 不通过 chat() 端到端
- ✅ **NodeExecutor 单元级**: 直接构造 GenerateSubgraphNode + executor.run(node)
- ✅ 已能用 mock DSL 验证核心语义 (callback 触发 + graph 接收)

## 错误码映射 (Phase G, 修正版)

**真实枚举** (llm_types.h:27-36): `NetworkError / RateLimited / AuthenticationError / Cancelled / InvalidRequest / ServerError / ContextOverflow / Unknown`

| 测试 | 旧 spec 错误码 | 真实枚举 | 备注 |
|---|---|---|---|
| G.2 Auth | `Code::Auth` | **`Code::AuthenticationError`** | deepseek API 401 |
| G.3 Timeout | `Code::Timeout` | **`Code::NetworkError`** | cpp-httplib 超时映射 (cloud_adapter.cpp:322), 无专属枚举 |
| G.4 Network | `Code::Network` | **`Code::NetworkError`** | invalid host 无 DNS 解析 |

**E.3 关键注记**: 必须 `max_retries=0` (默认 3, llm_config.h:73 — 否则 1s × 3 = 3s+ 等待)

## CI 集成设计 (前瞻)

**当前状态** (实测):
- ✅ `AGENTICDSL_BUILD_EXAMPLES=OFF` (CMakeLists.txt:209) → CI 不构建 examples → 当前 CI 不受影响
- ⚠️ 一旦未来启用 examples,fork PR 拿不到 secrets → 硬 FAIL → 红 PR

**修复** (Phase F.1):

```yaml
# .github/workflows/ci.yml (示例,具体 syntax 需匹配现有 workflow)
jobs:
  build-and-test:
    env:
      HYDRAFORGE_SKIP_REAL_LLM: "1"  # 新增
    steps:
      - ...
```

**手动真实验证** (留 `workflow_dispatch`):
```yaml
  real-llm-validation:
    if: github.event_name == 'workflow_dispatch'
    secrets: [DEEPSEEK_API_KEY, MINIMAX_API_KEY]
    env:
      HYDRAFORGE_SKIP_REAL_LLM: ""  # 覆盖 ci 默认 skip
    steps: [...]
```

## 与现有测试兼容性

- ✅ `test_e2e_real_llm.cpp` 现有 2 tests 行为变更 (WARN → FAIL + 移除 RUN_REAL_LLM gate)
- ⚠️ `HYDRAFORGE_RUN_REAL_LLM=1` 旧设置保留兼容 (helper 不读,继续 skip)
- ✅ 所有现有 ctest baseline 219 tests 必须保持 PASS
- ✅ 新增测试**仅在 `examples/` 树**构建 — 不进入 root ctest baseline

## 实施顺序

1. **Phase A** (命令单元) — 优先,无需 LLM/网络/CMake 复杂改动
2. **Phase C.1** (helper 抽离) — Phase C-real/D/G 依赖
3. **Phase B** (GenerateSubGraph + Mock) — 确定性,可在 helper 之前/之后(独立)
4. **Phase C-real** (真实 LLM GenerateSubGraph) — 需 API key
5. **Phase D** (多轮 context) — 需 API key
6. **Phase G** (错误处理) — 需 API key (G.4 网络可达性测试可 mock)
7. **Phase F** (CI + 验证 + archive)

每 phase 完成后 `cmake --build build + ctest -R <new tests>` 再启下一 phase。