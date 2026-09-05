# HydraForge 多轮对话架构文档

**生成日期**: 2026-09-04
**最后验证**: 2026-09-04（v0.5 — 第二轮审计修正 3 处：§二.4.1 deque 声明语法 + §二.4.2 is_retryable_error 位置 + §三.3 meta/args 区分）
**作者**: Architecture Working Group
**状态**: 🔍 Proposed（草案 v0.5）

**关联文档**:
- `docs/architecture/usage-flow-issues.md` — 混淆点与架构缺失清单（本文件锚定到 issues doc）
- `docs/specs/architecture.md` — AgenticOS 五层架构规范（L0~L4 + R1~R5）
- `docs/architecture/agent-orchestration-architecture-2026-08.md` — 5 层编排模型
- `docs/architecture/capability-application-map-2026-08.md` — 31 项已 ship 能力 + 9 项 open gap

---

## §〇 TL;DR（一分钟速览）

### 定位一句话

**HydraForge = 多轮对话 Agent 框架**（C++ / PDK Plugin），用户通过 pdk_chat_demo 与 Agent 进行多轮交互，每轮上下文（Session + Context）跨轮保持，Agent 记忆连贯。

### 4 步多轮对话主干

```
用户输入 → ChatSession::chat() 接收 + emit user.input
→ ChatSession 调用 call_tool("loop/run")（loop_agent 插件）
→ loop_agent 加载 .agent.md → DSLEngine 编译执行 → ReactLoop 单轮 ReAct
→ 事件总线广播（loop.turn.start / loop.decision / loop.turn.end）
→ ChatSession 提取结果 → emit loop.done → save_to_disk()（原子 .json 写入）
→ 返回响应给用户 → 等待下一轮
```

### 关键特性

- **Session 跨轮保持**：UserSession 持有 deque<TaskSession>，地址稳定
- **失败自动分裂**：TaskSession 失败计数 ≥3 → 新建 TaskSession
- **分支追溯**：`--fork <node_id>` 从历史节点分叉
- **持久化**：每轮 save_to_disk() 原子写入 .json（tmp + rename），支持重启恢复

> **混淆点与架构缺失**: 详见 `docs/architecture/usage-flow-issues.md`

---

## §一 多轮对话的入口与初始化

### 1.1 三种入口（聚焦 pdk_chat_demo CLI）

| 入口 | 场景 | 代码入口 | 适用人群 |
|------|------|---------|---------|
| **A：嵌入式 C++** | 在自己的 C++ 应用中集成对话能力 | `DSLEngine::run(UserSession&, ...)` | 应用开发者 |
| **B：PDK Plugin** | 加载 .so 扩展 Agent 能力 | `DSLEngine::load_plugin()` | 框架扩展者 |
| **C：pdk_chat_demo CLI** | 多轮对话应用开发（**主战场**） | `main.cpp` + `ChatSession` | 终端用户 |

pdk_chat_demo CLI 是多轮对话的**主战场**，所有会话管理、持久化、分支功能都围绕它实现。

**入口 A 补充**：嵌入式 C++ 走 `DSLEngine::run(UserSession&, message, LayeredContext)` [engine.cpp:461-499]，由应用代码直接驱动。pdk_chat_demo（入口 C）**不走这条路径**——它通过 `call_tool("loop/run")` 委托给 loop_agent 插件（见 §二.2）。

### 1.2 启动顺序（实证）

运行 `pdk_chat_demo --mock` 时，控制台输出：

```
[main] Mock mode: provider=mock, model=test
[INFO] [PluginLoader] loaded plugin: chat.loop v0.1.0
[main] Loaded plugin: chat.loop from build/pdk/loop_agent/libLoopAgent.so
[INFO] [PluginLoader] loaded plugin: infra.provider v0.1.0
[INFO] [PluginLoader] loaded plugin: infra.session v0.1.0
[INFO] [PluginLoader] loaded plugin: infra.budget v0.1.0
[INFO] [PluginLoader] loaded plugin: tool.fs v0.1.0
[INFO] [PluginLoader] loaded plugin: tool.shell v0.1.0
[main] Session started: sess_d9b22ea5e8512625f437bfc5c5cb2eb5
[main] Type 'exit' or Ctrl-D to quit
```

**启动顺序**（代码路径）：
```
main() [main.cpp:106]
  ├─ ChatConfig::from_json() [chat_session.cpp:79] 加载配置
  ├─ --mock → config.override_provider("mock", "test") [main.cpp:163]
  ├─ PluginLoader::load_so() [plugin_loader.cpp] 加载各 .so
  │    ├─ loop_agent.so      → 注册 1 个工具: loop/run
  │    ├─ provider_agent.so  → 注册 7 个工具: provider/register, resolve, list,
  │    │                       health, refresh, register_dynamic, switch
  │    ├─ session_agent.so   → 注册 5 个工具: session/history, branch, compact,
  │    │                       persist, search
  │    ├─ budget_agent.so    → 注册 4 个工具: budget/query, set_limit, alerts,
  │    │                       cost_breakdown
  │    ├─ fs_tools.so        → 注册 4 个工具: fs/read, fs/write, fs/list, fs/exists
  │    └─ shell_tools.so     → 注册 3 个工具: shell/exec, shell/which, shell/env
  ├─ DSLEngine::load_plugin() [engine.cpp:125] 注册所有工具
  ├─ ChatSession 构造 [chat_session.cpp:244] 创建 session_id
  │    └─ --session <id> 恢复（若有）[main.cpp:450]
  └─ 等待用户输入 [main.cpp:547 主循环]
```

**证据文件**:
- `examples/pdk_chat_demo/main.cpp:106` — main 入口
- `src/modules/plugin/plugin_loader.cpp` — PluginLoader::load_so
- `pdk/loop_agent/src/pdk_entry.cpp:164,204` — `pdk_register_tools`

### 1.3 CLI 标志矩阵（基于 --help 实测）

| 标志 | 说明 | 代码入口（已核实）|
|------|------|---------|
| `--mock` | 使用 MockLLMProvider，不发网络请求 | `main.cpp:163` |
| `--session <ID>` | 加载已有 session 继续 | `main.cpp:450`（load）、`:492`（优先级）、`:499`（错误）|
| `--fork <NODE_ID>` | 从历史节点分叉新分支 | `main.cpp:525`（验证）、`:530`（错误）|
| `--name <NAME>` | 给新 session 命名（与 --session 互斥）| `main.cpp:149` |
| `--provider <NAME>` | 覆盖配置的 provider | `cli_args_parser.cpp:12` + `main.cpp:160` |
| `--system-prompt <TEXT>` | 覆写系统提示（解析在 cli_args_parser）| `cli_args_parser.cpp:16` + `main.cpp:169-171` |
| `--append-system-prompt <TEXT>` | 追加系统提示（解析在 cli_args_parser）| `cli_args_parser.cpp:17` + `main.cpp:169-171` |
| `--allow-training-capture` | 允许训练数据采集（mock 模式拒绝）| `cli_args_parser.cpp:18` + `main.cpp:126` |
| `--print` | 打印配置后退出 | `cli_args_parser.cpp:11` + `:42` |
| `--offline` | 离线模式（无网络）| `cli_args_parser.cpp:13` + `:43` |

**说明**：系统提示默认值来自 `examples/pdk_chat_demo/config.json` `"agent.system_prompt"` 字段（config.json:49），运行时通过 `ChatConfig::override_system_prompt()` [chat_session.cpp:149-158] 覆写。

---

## §二 多轮对话的数据流追踪

### 2.1 4 类核心数据结构

| 数据结构 | 用途 | 生命周期 | 关键类 | 典型大小 |
|---------|------|----------|--------|---------|
| **Session** | 多轮会话根，持有 TaskSession 列表 | 多轮（持久化）| `UserSession`（持有 deque<TaskSession>）| ~1KB/轮 |
| **Context** | DSL 图内节点间数据传递 | 单次 DSL 执行 | `LayeredContext`（L1-L5）| <50KB |
| **ToolResult** | 工具调用返回值 | 单次调用 | `ToolResult{ok, data, error_code, latency_ms}`| <10KB |
| **Event** | 跨组件通知 | 异步广播 | `BusEvent{topic, payload, meta}`| <1KB/事件 |

#### UserSession / TaskSession 结构（ADR-0079）

```
UserSession
├── session_id:        唯一标识 "sess_xxxxxxxx"
├── task_sessions_:    deque<TaskSession>（地址稳定，push_back 不重新分配）
├── current_task_:      TaskSession* 指针（当前活跃任务）
├── messages:           deque<ToolResult>（对话历史）
└── metadata:           nlohmann::json（session 级元数据）

TaskSession
├── subtask_sessions_: deque<SubtaskSession>
├── policy_:            shared_ptr<IExecutionPolicy>
├── failure_count_:     int（可重试错误递增，≥3 分裂）
├── context_:          LayeredContext（当前任务上下文）
└── status_:          string（默认 "active"，成功后 "completed"，失败后 "failed"）
```

**deque 地址稳定性**: `push_back` 不重新分配，TaskSession 指针在多轮中保持有效。

**status 取值**（session.h:104 默认 `"active"`；engine.cpp:494 设 `"completed"`/`"failed"`）。

**证据文件**: `src/core/types/session.h`（UserSession / TaskSession / SubtaskSession 定义）

### 2.2 多轮对话端到端数据流大图

```
┌────────────────────────────────────────────────────────────────────────────┐
│                         多轮对话端到端数据流                                      │
│  关键特性：chat() 委托 loop/run 插件（非 engine.run）+ Session 跨轮保持            │
└────────────────────────────────────────────────────────────────────────────┘

  ┌─────────────────────────────────────────────────────────────────┐
  │  第 N 轮输入                                                    │
  │  用户: "帮我查一下天气"                                        │
  └────────────────────────────┬────────────────────────────────────┘
                               │
                               ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │  ChatSession::chat(input) [chat_session.cpp:294]                   │
  │  ├─ 追加 user 消息到 history [line 312]                        │
  │  ├─ emit "user.input" 事件 [line 318-321]                        │
  │  │    payload: {"input": user_input}                          │
  │  └─ 构造 loop_args [line 329-330]                              │
  │       {prompt, history, tools, system_prompt, loop_type,          │
  │        bus_ptr, session_id, cancellation_id}                   │
  └────────────────────────────┬────────────────────────────────────┘
                               │ [同步 call_tool]
                               ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │  call_tool("loop/run", loop_args) [chat_session.cpp:339]           │
  │  ↓ dispatch 到 loop_agent 插件的 loop/run handler                │
  │  [pdk/loop_agent/src/pdk_entry.cpp:164]                          │
  │  ├─ 校验 loop_type (react/plan_execute/fork_join) [line 225]     │
  │  ├─ load_agent_file(loop_type) [line 119-138]                   │
  │  │    └─ 读 lib/loop/<loop_type>.agent.md                      │
  │  └─ DSLEngine::from_markdown(agent_content) + child->run(ctx)     │
  │       [line 277-280]                                            │
  └────────────────────────────┬────────────────────────────────────┘
                               │
                               ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │  loop_agent 内部事件发射（emit_loop_event, 带 session_id）         │
  │  [pdk_entry.cpp:75-83]                                           │
  │  主题序列: loop.turn.start → loop.decision → loop.turn.end       │
  │  [异步] emit 入队即返，dispatch_thread 后台分发                    │
  └────────────────────────────┬────────────────────────────────────┘
                               │
                               ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │  LoopResult JSON 返回 → chat() 直接提取字段 [chat_session.cpp:348-351]│
  │  result.response   = loop_result.value("response", "")          │
  │  result.total_steps = loop_result.value("steps", 0)             │
  │  result.total_tokens = loop_result.value("tokens_used", 0)      │
  │  result.cost_usd    = loop_result.value("cost_usd", 0.0)      │
  └────────────────────────────┬────────────────────────────────────┘
                               │
                               ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │  chat() 后处理 [chat_session.cpp:352-406]                        │
  │  ├─ 追加 assistant 消息到 history [line 357-362]                  │
  │  ├─ emit "loop.done" [line 366-373]                             │
  │  ├─ budget 检查 (exceeded → emit budget.checked + fail)          │
  │  └─ emit "session.persist_request" [line 400-403]               │
  └────────────────────────────┬────────────────────────────────────┘
                               │
                               ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │  save_to_disk() 原子写入 [chat_session.cpp:480-533]                │
  │  ├─ 写 path + ".tmp" [line 497-507]                            │
  │  ├─ rename(tmp, path) [line 510-515]                           │
  │  ├─ 失败 → 清理 tmp + 返回 false [line 516-521]                  │
  │  └─ 成功 → emit "session.persisted" [line 522-531]              │
  └────────────────────────────┬────────────────────────────────────┘
                               │
                               ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │  第 N 轮输出 → std::cout → 第 N+1 轮就绪                          │
  │  Session 状态: 持久化完成，等待新输入                              │
  └─────────────────────────────────────────────────────────────────┘

═══════════════════════════════════════════════════════════════════════
  关键事实：chat() 不调用 engine.run(UserSession&, ...)
═══════════════════════════════════════════════════════════════════════

  engine.run(UserSession&, message, LayeredContext) [engine.cpp:461-499]
  是入口 A（嵌入式 C++）的路径，不在 pdk_chat_demo 的 chat() 路径中。

  pdk_chat_demo 的 chat() 只做：
    1. emit "user.input"
    2. call_tool("loop/run", loop_args)  ← 委托给 loop_agent 插件
    3. 从 loop_result JSON 提取字段
    4. emit "loop.done" + 持久化

  DSL 编译执行发生在 loop_agent 插件内部（pdk_entry.cpp），
  不在 ChatSession 中。
```

### 2.3 单轮数据流 5 步追踪（每步带 file:line）

#### 步骤 1：用户输入 → ChatSession::chat() 接收 + emit user.input

| 维度 | 内容 |
|------|------|
| **触发条件** | 用户在终端输入文字并回车 |
| **代码位置** | `examples/pdk_chat_demo/chat_session.cpp:294` — `ChatResult ChatSession::chat(const std::string& user_input)` |
| **处理逻辑** | 1. 追加 user 消息到 history [line 312]；2. emit `user.input` 事件 [line 318-321]（EventBuilder + payload `{"input": user_input}`）|
| **典型耗时** | <1ms（输入读取） |
| **同步/异步** | 同步（stdin 读取阻塞） |
| **失败模式** | stdin 读取失败（终端关闭）|

#### 步骤 2：ChatSession 构造 loop_args 并调用 loop/run

| 维度 | 内容 |
|------|------|
| **触发条件** | chat() 完成输入接收后 |
| **代码位置** | `examples/pdk_chat_demo/chat_session.cpp:329-339` |
| **loop_args 字段** | `prompt` [line 329]、`system_prompt` [line 330]、`history`、`tools`、`loop_type`、`bus_ptr`、`session_id`、`cancellation_id` |
| **关键调用** | `nlohmann::json loop_result = impl_->registry->call_tool("loop/run", loop_args);` [line 339] |
| **典型耗时** | <1ms（call_tool 分发） |
| **同步/异步** | 同步（call_tool 阻塞直到插件返回） |
| **失败模式** | loop/run 工具未注册（registry 抛异常）|

#### 步骤 3：loop_agent 插件内部执行（load .agent.md → DSLEngine → Loop）

| 维度 | 内容 |
|------|------|
| **触发条件** | `loop/run` handler 被调用 [pdk_entry.cpp:164] |
| **代码位置** | `pdk/loop_agent/src/pdk_entry.cpp:221-280` |
| **Loop 选择依据** | `loop_type` 参数决定加载哪个 `.agent.md` 文件（`lib/loop/<loop_type>.agent.md`）[line 119-138]：`react.agent.md` / `plan_execute.agent.md` / `fork_join.agent.md`。合法性校验仅允许 react / plan_execute / fork_join [line 225] |
| **DSL 编译执行** | `DSLEngine::from_markdown(agent_content, *tls_parent_provider)` [line 277] → `child->run(ctx)` [line 280]。ProviderLLMTool 包装父 provider 注入（模型名取父 provider 模型列表首位）|
| **Mock fallback** | 父 provider 未设置时返回 mock 响应 [line 240-256]（steps=1, tokens=42, cost=0.001）|
| **典型耗时** | Mock 模式 <100ms；含真实 LLM 1-5s |
| **同步/异步** | 同步（child->run 阻塞）|
| **失败模式** | .agent.md 文件不存在（load_agent_file 抛异常）；cancellation_token 已停止（提前返回 cancelled）[line 260-268] |

> **注意**：ReactLoop 类（`include/agenticdsl/pdk/agent_loops/react_loop.h:80-138`）是 PDK 库中的独立实现，内部委托 `SimpleCognitiveOrchestrator` 单轮 ReAct。loop_agent 插件当前通过加载 `.agent.md` DSL 文件执行，与 ReactLoop 类是两条并行路径。

#### 步骤 4：事件总线广播（loop.turn.start / decision / end）

| 维度 | 内容 |
|------|------|
| **触发条件** | loop_agent 插件执行期间调用 `emit_loop_event()` |
| **代码位置** | `pdk/loop_agent/src/pdk_entry.cpp:75-83`（emit_loop_event 定义）、`:326,330,336`（发射点）|
| **事件序列** | `loop.turn.start` → `loop.decision` → `loop.turn.end` |
| **payload 结构** | 每个事件都带 `session_id` 字段（emit_loop_event 注入）[line 80] |
| **loop.turn.start** | `{"turn": 1, "step": 1, "session_id": "..."}` [line 326-327] |
| **loop.decision** | `{"decision": "tool_call", "tool": "loop/run", "session_id": "..."}` [line 330-331] |
| **loop.turn.end** | `{"turn": 1, "decision": "respond"|"give_up", "session_id": "..."}` [line 336-337] |
| **bus 类型** | `InMemoryBus`（异步，emit 入队即返）|
| **测试证据** | `test_e2e_mock.cpp:78-173`（验证事件序列与 payload 键存在）|

#### 步骤 5：结果提取 + Session 持久化（原子写入）

| 维度 | 内容 |
|------|------|
| **触发条件** | loop/run 返回后 |
| **代码位置** | `examples/pdk_chat_demo/chat_session.cpp:348-351`（字段提取）、`:357-362`（assistant 消息）、`:366-373`（loop.done）、`:400-405`（persist_request + save_to_disk）、`:480-533`（save_to_disk 实现）|
| **字段提取** | `response` / `steps` / `tokens_used` / `cost_usd` 直接取自 loop_result JSON [line 348-351] |
| **持久化序列** | 1. emit `session.persist_request` [line 400-403]；2. 同步调用 `save_to_disk()` [line 405] |
| **原子写入** | 写 `path + ".tmp"` [line 497-507] → `rename(tmp, path)` [line 510-515]；失败清理 tmp [line 516-521] |
| **成功事件** | 仅在原子 rename 成功后 emit `session.persisted` [line 522-531] |
| **schema 版本** | `kSessionSchemaVersion` = 1 |
| **典型耗时** | <10ms（单轮写入）|
| **失败模式** | tmp 写入失败（返回 false）；rename 失败（清理 tmp + 返回 false）|

### 2.4 多轮之间的状态保持（关键特性）

#### 2.4.1 Session 状态保持

```
第 1 轮完成后:
  UserSession.task_sessions_ = [TaskSession_0]  (deque，地址稳定)
  TaskSession_0.status_ = "completed"  (engine.cpp:494)

第 2 轮输入:
  ctx["user_input"] = "继续刚才的话题"
  TaskSession_0 保持，指针不变
```

**deque 地址稳定性**（`src/core/types/session.h:143`）：
```cpp
std::deque<TaskSession> task_sessions_;  // deque 确保 current_task_session_ 地址稳定
```

#### 2.4.2 失败自动分裂

**完整失败计数序列**（3 处代码位置）：
```cpp
// ① 入口检查（engine.cpp:474-476）：判断是否需新建 TaskSession
if (task_sess_ptr->determine_failure_mode() == TaskSession::FailureMode::NewSession) {
    task_sess_ptr = &user_sess.create_task_session();  // 新建 TaskSession
}

// ② 实际计数（engine.cpp:488）：每轮执行后递增
task_sess_ptr->record_failure(result);

// ③ 计数规则（session.h:148-159 is_retryable_error）：
//    仅 Retry / Timeout / ResourceExhausted 递增 failure_count_
```

**判断逻辑**：`record_failure()` 内部检查结果是否可重试错误；`determine_failure_mode()` 在 failure_count_ ≥ 3 时返回 NewSession。

**可重试错误类型**（tool_result.h:37-42）：
- Retry（建议重试，NetworkError 透传）
- Timeout（工具执行超时）
- ResourceExhausted（资源耗尽：内存/磁盘/句柄）

#### 2.4.3 Context 轮间合并（嵌入式入口 A 路径）

```
前轮 TaskSession.context_.working [engine.cpp:482 set_context]
  → 作为新轮 LayeredContext.L3 task 层输入
  → 新轮执行过程中 L4 node 层写入
  → 执行完成后 L4 node 层合并回 TaskSession.context_.working
```

> **注意**：此机制适用于入口 A（`engine.run(UserSession&, ...)`）。pdk_chat_demo（入口 C）的上下文保持依赖 `history` 数组（user/assistant 消息逐轮累积），通过 `loop_args["history"]` 传给 loop_agent [chat_session.cpp:329]。

### 2.5 典型时序

| 场景 | 耗时 | 说明 |
|------|------|------|
| Mock 单轮（无 LLM）| <100ms | loop_agent mock fallback 直接返回 |
| 真实 LLM 单轮 | 1-5s | 取决于模型和网络 |
| 含工具调用（1 步）| +100-500ms | 工具执行时间 |
| Session 持久化 | <10ms | 原子 .json 写入 |
| 多轮（10 轮）| 1-10s | 取决于 LLM |

### 2.6 取消链（Ctrl+C → Loop token → Provider）

```
Ctrl+C 信号
  → signal_handler [main.cpp:75-82] 仅置位 g_shutdown_requested (atomic, line 67)
    → main loop 字节检测 flag [main.cpp:548]
      → engine.reset() → unload_all_plugins(loader) [main.cpp:93/100, 628]
        → CancellationRegistry 取消 in-flight 请求 [chat_session.cpp cancellation_registry]
          → stop_token 传播到 loop_agent [pdk_entry.cpp:260-268]
            → dispatch_to_tool(std::stop_token) [node_executor.cpp:450]
              → tool_coordinator_->execute(..., token) [tool_coordinator.cpp:349]
                → provider->generate(req, token) [cloud_adapter.cpp:311]
```

**证据文件**（已核实）:
- `examples/pdk_chat_demo/main.cpp:65`（注释）、`:67`（g_shutdown_requested）、`:75-82`（signal_handler）、`:548`（主循环检查）
- `examples/pdk_chat_demo/cancellation_registry.h`（CancellationRegistry）
- `pdk/loop_agent/src/pdk_entry.cpp:260-268`（cancellation_token 检查 + cancelled 返回）
- `include/agenticdsl/pdk/agent_loops/react_loop.h:87`（stop_token 检查）

---

## §三 多轮对话的实证证据

### 3.1 复现命令

```bash
# 运行所有多轮对话相关测试
cd build/examples/pdk_chat_demo/tests

# test_e2e_mock: 端到端多轮对话（42 assertions / 7 cases）
./test_e2e_mock 2>&1 | tail -5

# test_chat_session_events: 事件总线验证（34 assertions / 6 cases）
./test_chat_session_events 2>&1 | tail -5

# test_session_persistence: Session 持久化/恢复/cleanup（20 assertions / 5 cases）
./test_session_persistence 2>&1 | tail -5

# test_session_tree_commands: Session 分支命令（8 assertions / 2 cases）
./test_session_tree_commands 2>&1 | tail -5

# test_pdk_chat_demo_session_tree_cli_flags: CLI 标志解析（17 assertions / 6 cases）
./test_pdk_chat_demo_session_tree_cli_flags 2>&1 | tail -5
```

### 3.2 实证表

| 测试文件 | 测试内容 | Cases | Assertions | PASS |
|---------|---------|:------:|:---------:|:----:|
| `test_e2e_mock.cpp` | 端到端多轮对话（Mock 模式）| 7 | 42 | ✅ |
| `test_chat_session_events.cpp` | 事件总线订阅/广播 | 6 | 34 | ✅ |
| `test_session_persistence.cpp` | Session 持久化/恢复/cleanup | 5 | 20 | ✅ |
| `test_session_tree_commands.cpp` | Session 分支（--fork）| 2 | 8 | ✅ |
| `test_pdk_chat_demo_session_tree_cli_flags.cpp` | CLI 标志解析 | 6 | 17 | ✅ |
| **合计** | | **26** | **121** | **✅** |

### 3.3 关键事件 payload 示例

> **注意**：以下 payload 值为示例。`test_e2e_mock.cpp:147-150` 仅验证 `payload.contains("turn")` 和 `payload.contains("step")`（键存在），**不验证具体值**。`emit_loop_event` 在每个事件注入 `session_id` 字段 [pdk_entry.cpp:80]。

#### user.input（chat_session.cpp:318-321 发射）
```json
{"input": "帮我查一下天气"}
```

#### loop.turn.start（pdk_entry.cpp:326-327 发射，值示例）
```json
{"turn": 1, "step": 1, "session_id": "sess_d9b22ea5..."}
```

#### loop.decision（pdk_entry.cpp:330-331 发射，值示例）
```json
{"decision": "tool_call", "tool": "loop/run", "session_id": "sess_d9b22ea5..."}
```

#### loop.turn.end（pdk_entry.cpp:336-337 发射，值示例）
```json
{"turn": 1, "decision": "respond", "session_id": "sess_d9b22ea5..."}
```

#### loop.done（chat_session.cpp:366-373 发射；EventBuilder args=业务字段 / meta=session_id）
```json
// args（业务字段）
{"response": "好的，我已经帮你写入文件...", "total_steps": 1, "total_tokens": 42}
// meta（trace 上下文）
{"session_id": "sess_d9b22ea5..."}
```

#### session.persist_request（chat_session.cpp:400-403 发射；EventBuilder args=业务字段 / meta=session_id）
```json
// args（业务字段）
{"messages": [{"role": "user", "content": "..."}, {"role": "assistant", "content": "..."}]}
// meta（trace 上下文）
{"session_id": "sess_d9b22ea5..."}
```

#### session.persisted（chat_session.cpp:522-531 发射，仅在原子 rename 成功后；EventBuilder args=业务字段 / meta=session_id）
```json
// args（业务字段）
{"session_id": "sess_d9b22ea5e8512625f437bfc5c5cb2eb5", "path": "~/.hydraforge/sessions/sess_d9b22ea5.json"}
```

### 3.4 Session 文件结构（.json 单对象，非 JSONL）

**实际格式**：`save_to_disk()` 写入**单个 JSON 对象**（`j.dump(2)`，chat_session.cpp:497-507），文件扩展名 `.json`。**不是** JSONL。

```json
{
  "schema_version": 1,
  "session_id": "sess_d9b22ea5e8512625f437bfc5c5cb2eb5",
  "created_at": 1757072345000,
  "updated_at": 1757072401000,
  "provider_mode": "mock",
  "budget": {
    "total": 10.0,
    "used": 0.003
  },
  "history": [
    {"role": "user", "content": "帮我创建一个文件", "timestamp": 1757072345000},
    {"role": "assistant", "content": "好的，正在写入...", "timestamp": 1757072401000, "steps": 1, "tokens": 42}
  ]
}
```

**原子写入保证**（chat_session.cpp:480-533）：
1. 写入 `path + ".tmp"` [line 497-507]
2. `fs::rename(tmp, path)` [line 510-515] — 原子替换
3. rename 失败 → 清理 tmp → 返回 false [line 516-521]
4. 仅在成功后 emit `session.persisted` [line 522-531]

**schema 版本检查**（test_session_persistence.cpp）：
- `kSessionSchemaVersion` = 1，接受
- v999：返回 false（不抛异常）
- load_from_disk 恢复 UserSession

### 3.5 如何重现这些证据

```bash
# 1. 构建测试
cd build
cmake --build . --target test_e2e_mock test_chat_session_events \
  test_session_persistence test_session_tree_commands \
  test_pdk_chat_demo_session_tree_cli_flags

# 2. 运行单个测试，查看详细输出
cd examples/pdk_chat_demo/tests
./test_e2e_mock --verbosity high 2>&1 | less

# 3. 检查 Session 文件实际写入（.json 单对象）
ls -la ~/.hydraforge/sessions/
cat ~/.hydraforge/sessions/sess_*.json | python3 -m json.tool | head -30

# 4. 对比事件订阅（启用 debug 输出）
./test_e2e_mock --log-level debug 2>&1 | grep "loop.turn"
```

---

## §四 典型多轮场景

### 场景 A：基本多轮对话（3 轮上下文保持）

```
第 1 轮:
  用户: "帮我创建一个文件"
    → chat() → call_tool("loop/run") → fs/write → 成功
    → loop_result.response + history 追加 assistant 消息
    → save_to_disk() 原子写入 .json

第 2 轮:
  用户: "查看刚才创建的文件"
    → chat() → loop_args["history"] 包含第 1 轮完整对话
    → loop/run → fs/read → 读取文件内容
    → history 追加第 2 轮消息

第 3 轮:
  用户: "总结一下刚才的内容"
    → history 已含前两轮全部消息（user + assistant）
    → LLM 可以引用完整上下文
```

**关键特性**: pdk_chat_demo 的上下文保持依赖 `history` 数组逐轮累积（chat_session.cpp:312, 357-362），通过 `loop_args["history"]` 传给 loop_agent [chat_session.cpp:329]。

### 场景 B：会话分支（--fork 追溯历史）

```
当前 Session: sess_abc
  task_sessions_[0]: 创建文件（成功，status="completed"）
  task_sessions_[1]: 读取文件（成功）
  task_sessions_[2]: 删除文件（当前）

用户执行: --fork <node_id>（main.cpp:525 验证节点存在性）
  → 从指定节点分叉新 session（main.cpp:530 错误处理）
  → 新 session 继承源 session 历史，从分叉点继续
```

**证据**: test_session_tree_commands.cpp（--fork 测试 2 cases / 8 assertions PASS）；main.cpp:525（验证）、:530（错误）

### 场景 C：会话恢复（--session 继续历史）

```
用户退出后重启:
  ./pdk_chat_demo --session sess_abc

main.cpp:450 从磁盘恢复
  → load_from_disk("sess_abc")（chat_session.cpp）
  │    读取 ~/.hydraforge/sessions/sess_abc.json
  │    schema 版本检查（v1 接受，v999 拒绝）
  ├─ 重建 messages 历史
  ├─ --session 与 --name 互斥检查 [main.cpp:149]
  └─ 继续 chat() 等待用户输入
```

**证据**: test_session_persistence.cpp（load_from_disk 5 assertions PASS）；main.cpp:450（load）、:492（优先级）、:499（错误）

> 其他场景（单轮 ReAct / 审批流 / 多智能体并行）：详见 `docs/architecture/usage-flow-issues.md`

---

## §五 文档索引

### 核心架构

| 文档 | 路径 | 说明 |
|------|------|------|
| 五层架构规范 | `docs/specs/architecture.md` | L0-L4 + R1-R5 |
| 多领域智能体架构 | `docs/architecture/multi-domain-agent-architecture.md` | Cognitive/Domain 分层 |
| **混淆点与缺失** | `docs/architecture/usage-flow-issues.md` | **本文件锚定** |

### ADR（会话相关）

| ADR | 议题 | 状态 |
|-----|------|------|
| ADR-0033 | Session 3 层 | ✅ Approved |
| ADR-0079 | Session 4-Scope | ✅ Approved |
| ADR-0068 | 事件发射契约 | ✅ Approved |
| ADR-0021 | PDK Loop Agent | ✅ Approved |

### 实证文件

| 文件 | 说明 |
|------|------|
| `examples/pdk_chat_demo/tests/test_e2e_mock.cpp` | 42 assertions / 7 cases PASS |
| `examples/pdk_chat_demo/tests/test_chat_session_events.cpp` | 34 assertions / 6 cases PASS |
| `examples/pdk_chat_demo/tests/test_session_persistence.cpp` | 20 assertions / 5 cases PASS |

---

## 附录 A：术语表

| 术语 | 定义 | 证据 |
|------|------|------|
| **Session** | 多轮会话根，持有 TaskSession 列表 | `src/core/types/session.h` |
| **TaskSession** | 单次任务执行上下文，含 failure_count + status | `src/core/types/session.h` |
| **SubtaskSession** | 子任务级（Fork/Join 分支）| `src/core/types/session.h` |
| **LayeredContext** | 5 层上下文（L1 system → L5 raw）| `include/agenticdsl/types/layered_context.h` |
| **ToolResult** | 工具调用返回值信封 | `src/core/types/tool_result.h` |
| **IInteractionBus** | 事件总线，27+ 主题 | `include/agenticdsl/contract/iinteraction_bus.h` |
| **ReactLoop** | PDK 库中的单轮 ReAct 类（委托 SimpleCognitiveOrchestrator）| `include/agenticdsl/pdk/agent_loops/react_loop.h` |
| **ChatSession** | 多轮对话编排器（emit user.input → call_tool loop/run → 持久化）| `examples/pdk_chat_demo/chat_session.h` |
| **emit_loop_event** | loop_agent 插件内事件发射助手（注入 session_id）| `pdk/loop_agent/src/pdk_entry.cpp:75-83` |

---

## 附录 B：复现命令

```bash
# 构建 pdk_chat_demo 及测试
cd build
cmake --build . --target pdk_chat_demo

# 运行所有多轮对话测试（26 cases / 121 assertions PASS）
ctest -R "pdk_chat_demo" --output-on-failure

# 运行单个测试
cd examples/pdk_chat_demo/tests
./test_e2e_mock 2>&1 | tail -3
./test_chat_session_events 2>&1 | tail -3
./test_session_persistence 2>&1 | tail -3
./test_session_tree_commands 2>&1 | tail -3
./test_pdk_chat_demo_session_tree_cli_flags 2>&1 | tail -3

# 验证 Session 持久化（.json 单对象 + 原子写）
ls ~/.hydraforge/sessions/
cat ~/.hydraforge/sessions/sess_*.json | python3 -m json.tool | head -30

# 验证 --help 输出
./pdk_chat_demo --help 2>&1 | head -20

# 验证 Mock 模式插件加载
./pdk_chat_demo --mock 2>&1 | grep "loaded plugin"

# 验证事件序列（debug 模式）
./pdk_chat_demo --mock 2>&1 | grep -E "loop\.(turn|decision|done)"
```

---

## 变更记录（v0.3 → v0.4 主要修正）

| # | 修正内容 |
|---|---------|
| 1 | `ChatSession::chat()` 行号 244 → 294（244 是构造函数）|
| 2 | §二.2 数据流图重写：chat() 调 `call_tool("loop/run")` 而非 `engine.run()` |
| 3 | Session 文件结构：JSONL → 单 JSON 对象（.json + 原子写）|
| 4 | 删除不存在的 `process_output_keys()`，改为直接字段提取 |
| 5 | TaskSession status 枚举：idle/running → active/completed/failed |
| 6 | 事件 payload 示例：加 session_id 字段 + 注明值仅示例 |
| 7 | CLI 标志行号纠正（--mock:163, --session:450/492/499, --fork:525/530, --name:149）|
| 8 | 插件工具清单补全（provider 7 / session 5 / budget 4 / fs 4 / shell 3 / loop 1）|
| 9 | 持久化序列补全：persist_request → save_to_disk（tmp+rename）→ persisted |
| 10 | 失败计数补全：3 处代码位置（检查/计数/规则）|
| 11 | Loop 选择改为加载 .agent.md 文件（非类实例化）|
| 12 | 取消链行号核实（signal_handler:75-82, main loop:548 等）|
| 13 | chat() vs engine.run() 全文重构（嵌入式 A 走 engine.run，CLI C 走 call_tool）|

---

## 变更记录（v0.4 → v0.5 第二轮审计修正）

| # | 修正内容 |
|---|---------|
| 14 | §二.4.1 deque 声明语法纠正：`deque<UserSession::TaskSession>` → `std::deque<TaskSession> task_sessions_`（session.h:143）|
| 15 | §二.4.2 `is_retryable_error` 文件引用纠正：`tool_result.h:150-159` → `session.h:148-159` |
| 16 | §三.3 事件 payload 明确 args/meta 区分（chat_session.cpp 发射的 3 个事件：loop.done / session.persist_request / session.persisted）|