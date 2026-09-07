# PDK Chat Demo

> **Agent-as-Plugin 架构端到端验证 demo**
> 关联设计: [DESIGN.md](./DESIGN.md)
> 关联 ADR: ADR-0052 ~ ADR-0065

## 概述

`pdk_chat_demo` 演示 HydraForge "AgenticOS" 范式：

- **应用 = Agent 组合** — Chat 应用由 6 个独立 Agent Plugin 编排
- **万物皆 Agent，Agent 皆 Plugin** — 每个 Agent 是独立 .so
- **6 个 Agent 协作**: Chat / Loop / Provider / Session / Budget / FS / Shell

## 编译

```bash
# 在项目根目录
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DAGENTICDSL_BUILD_EXAMPLES=ON
make -j$(nproc)
```

编译完成后 `build/examples/pdk_chat_demo/pdk_chat_demo` 可用。

## 运行

### Mock 模式（CI 验证，零依赖）

```bash
./build/examples/pdk_chat_demo/pdk_chat_demo --mock
```

输入测试用例：

```
User> Write a hello world in C++
```

### 真实 LLM 模式（DeepSeek）

```bash
export DEEPSEEK_API_KEY=sk-...
./build/examples/pdk_chat_demo/pdk_chat_demo
```

默认使用 `deepseek` provider + `deepseek-v4-flash` 模型（DeepSeek API，OpenAI 兼容协议），对话输出真实的 LLM 回复：

```
User> Write a hello world in C++

Assistant: Here's a simple "Hello, World!" program in C++:

```cpp
#include <iostream>

int main() {
    std::cout << "Hello, World!" << std::endl;
    return 0;
}
```
```

### 事件输出

demo 同时输出结构化事件日志（时间戳 + topic）：

```
[23:58:49] user.input: Write a hello world in C++
[23:58:54] loop.done: total_steps=1, total_tokens=258
```

## 配置说明

配置文件 `examples/pdk_chat_demo/config.json` 可自定义：

| 字段 | 默认值 | 说明 |
|------|--------|------|
| `agent.provider` | `"deepseek"` | LLM 提供方 (deepseek/openai/anthropic/mock) |
| `agent.model` | `"deepseek-v4-flash"` | 模型名 |
| `agent.system_prompt` | — | 系统提示词 |
| `providers` | 4 个预配置 | 各 provider 的 api_url / api_key_env |

### 切换默认 provider

编辑 `config.json`：

```json
"agent": {
    "provider": "openai",
    "model": "gpt-4o"
}
```

并确保对应 provider 的 `api_key_env` 环境变量已设置。

## Plugin 构成

| Plugin | 形态 | 路径 | 角色 |
|--------|------|------|------|
| Chat Agent | C++ (`main.cpp`) | `examples/pdk_chat_demo/` | 编排器 + 交互循环 |
| Loop Agent | C++ (.so) | `pdk/loop_agent/` | DSL 执行器（当前 mock，需 ADR-0019） |
| Provider Agent | C++ (.so) | `pdk/provider_agent/` | LLM provider 注册与解析 |
| Session Agent | C++ (.so) | `pdk/session_agent/` | 多轮会话管理 |
| Budget Agent | C++ (.so) | `pdk/budget_agent/` | 预算控制 |
| FS Tools | C++ (.so) | `pdk/fs_tools/` | 文件系统工具 |
| Shell Tools | C++ (.so) | `pdk/shell_tools/` | Shell 命令执行 |
| Code Review | SKILL.md | `skills/code-review/` | 代码审查（mock-only, 需 ADR-0055） |

## DSL 示例

`examples/pdk_chat_demo/dsl/` 目录包含 PlanExecuteLoop 和 ForkJoinLoop 的示例 `.agent.md` 文件：

| 文件 | 循环类型 | 说明 |
|------|----------|------|
| `plan_execute_example.agent.md` | PlanExecuteLoop | 研究量子计算 — 3 阶段 (Planning→Executing→Verifying) |
| `fork_join_example.agent.md` | ForkJoinLoop | 并行多源搜索 — 4 阶段 (Forking→Executing→Joining) |

### PlanExecuteLoop 示例

展示 3 阶段循环: Planning (LLM 生成子图) → Executing (DSLEngine 执行) → Verifying (LLM 验证)

```bash
# 使用方式
PlanExecuteLoop loop(std::move(engine), bus);
LoopResult result = loop.run("研究量子计算的最新进展", ctx);
```

### ForkJoinLoop 示例

展示 4 阶段循环: Forking (并发派发) → Executing (并行 branch) → Joining (结果合并)

```bash
# 使用方式
ForkJoinLoop loop(std::move(engine), bus, /*num_threads=*/4);
std::vector<std::string> branches = {"search_web", "search_docs", "search_code"};
LoopResult result = loop.run(branches, ctx);
```

## 测试

```bash
cd build
ctest -R pdk_chat --output-on-failure
```

测试用例:

- `test_chat_session`: ChatSession 单元测试（5 个 test case, 25 断言）
- `test_e2e_mock`: 端到端 mock 模式测试（3 个 test case, 9 断言）

全量:

```bash
ctest -j$(nproc)
```

## 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| `'https' scheme is not supported` | cpp-httplib 编译时未启用 SSL | 确认安装了 `libssl-dev` 并重新 cmake |
| `Connection failed` | API endpoint 路径错误 | 确认 `config.json` 中 `api_endpoint` 正确 |
| `plugin registration: dangerous category` | ApprovalPolicy 未设置 plan/agent 审批 | 检查各 plugin 的 `ApprovalPolicy` |
| `LLM generation failed` | API key 无效或未设置 | `echo $DEEPSEEK_API_KEY` 确认已 export |
| demo 启动后无响应 | provider/resolve 死锁（已修复） | 更新至最新 commit |

## 设计文档

完整设计见 [DESIGN.md](./DESIGN.md)（784 行），包括:

- 完整架构图 + 组件关系
- JSON 配置 schema
- 6 个 Agent 详细设计
- 事件流 + 错误处理
- 与 ADR 的完整对照

## 相关文档

- `docs/architecture/agent-as-plugin-architecture-v1.1.md` — 总架构
- `docs/architecture/agent-evolution-pipeline.md` — 4 阶段管线
- `docs/adr/adr-0052-agent-plugin-manifest.md` — manifest 规范
- `docs/adr/adr-0054-capability-discovery.md` — Capability 索引
- `docs/adr/adr-0057-agent-lifecycle.md` — Plugin 生命周期
- `docs/adr/adr-0058-tool-schema-validation.md` — Schema 校验
- `docs/adr/adr-0060-agent-composition.md` — 6 种协作模式
