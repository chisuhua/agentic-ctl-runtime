# HydraForge 用户视角入口架构文档

**生成日期**: 2026-09-04
**最后验证**: 2026-09-04
**作者**: Architecture Working Group
**状态**: 🔍 Proposed（草案 v0.1, 等待架构组评审）

**关联文档**:
- `docs/specs/architecture.md` — AgenticOS 五层架构规范（L0~L4 + R1~R5）
- `docs/architecture/agent-orchestration-architecture-2026-08.md` — 5 层编排模型
- `docs/architecture/capability-application-map-2026-08.md` — 31 项已 ship 能力 + 9 项 open gap
- `docs/architecture/layer-based-missing-capabilities-analysis.md` — 五层缺失能力分析
- `examples/pdk_chat_demo/DESIGN.md` — 真实 demo 设计
- `src/core/engine.cpp` — 主入口实现
- `src/modules/scheduler/topo_scheduler.cpp` — DAG 调度实现
- `src/modules/executor/node_executor.cpp` — 节点执行器实现
- `src/common/tools/tool_coordinator.cpp` — 审批 + hook 实现

---

## §〇 TL;DR（一分钟速览）

### 定位一句话

**HydraForge = AgenticOS**（C++ Agent-as-Plugin 框架），通过 Markdown DSL 定义工作流图（DAG），支持 LLM 调用、工具注册、资源管理和预算控制，使用 llama.cpp 作为 LLM 后端。

### 三种典型入口

| 入口 | 场景 | 代码入口 | 适用人群 |
|------|------|---------|---------|
| **A：嵌入式 C++** | 把 HydraForge 当库嵌入自己的 C++ 应用 | `DSLEngine::from_markdown()` + `run()` | 应用开发者 |
| **B：动态加载 PDK Plugin** | Agent-as-Plugin 范式 | `DSLEngine::load_plugin()` + `.so` | 框架扩展者 |
| **C：CLI/TUI（pdk_chat_demo）** | 多轮对话应用开发 | `main.cpp` + `ChatSession` + 终端交互 | 终端用户 |

### 7 步数据流主干

```
用户输入 → ChatSession.chat() → loop_agent → Loop选择 
→ DSL解析 → ParsedGraph → TopoScheduler.execute() → DAG调度 
→ NodeExecutor.execute_node() → dispatch_to_tool() 
→ ToolCoordinator → ToolRegistry → Plugin → LLM/工具执行 
→ Context合并 → Trace输出
```

### 5 层架构速览（OS 视角）

```
L4: Agent 应用服务层（Temporal Agent / Loop Agent / G1）
L3: PDK 接口契约层（IToolRegistry / ITemporalClient / IAgentRegistry）
L2: Plugin 工具层（shell_tools / fs_tools / llama_engine）
L1: OS 服务层（IInteractionBus / IBudgetController / PluginLoader）
L0: Runtime Core（DSLEngine / NodeExecutor / MarkdownParser）
```

### 三大最关键混淆点

1. **L0-L4（OS 视角）vs L1-L5（编排视角）混用** — 两者都是 5 层但编号差 1，容易混淆
2. **MCP 一词多义** — 外部标准协议 vs LayeredContext 等自研协议
3. **ChatSession "直连 LLM" 分支已删除但文档未同步** — `fix-loop-agent-bypass` 修复（2026-08-07 ship）

### 五个最关键缺失

1. **G1: compact 破坏性重写** — ADR-0079 v1.2 决策未落地，会话无法真正压缩
2. **G3: AgentWorker 完整实现** — T3+T4 未完成，B2 跨进程协作阻塞
3. **G4: Agent↔Agent stream 模式** — T6 未完成，B4 流式 Agent 阻塞
4. **G6: Agent hook loop 集成** — IAgentHookRegistry 骨架已 ship，但与 Loop 集成未完成
5. **G9: ADR-0076 DSL Engine as MCP Server** — B5 DSL-as-MCP-tool 阻塞

---

## §一 用户视角的入口路径

### 1.1 三种入口详解

#### 入口 A：嵌入式 C++

**场景**: 把 HydraForge 当 C++ 库嵌入到自己的应用程序

**代码入口**:
```cpp
// src/core/engine.cpp:69
DSLEngine engine;
engine.from_markdown(markdown_dsl_content);  // 加载 DSL
auto result = engine.run(initial_context);     // 执行
```

**关键文件**:
- `src/core/engine.h/cpp` — DSLEngine 主类
- `src/modules/parser/markdown_parser.cpp` — Markdown → ParsedGraph
- `src/modules/scheduler/topo_scheduler.cpp` — DAG 调度
- `src/modules/executor/node_executor.cpp` — 节点执行
- `examples/agent_basic/main.cpp` — 最小示例

**适用人群**: 想在自己的 C++ 应用中集成 Agent 能力的开发者

**典型例子**: `examples/agent_basic/` — 单 agent 工具调用 ReAct

---

#### 入口 B：动态加载 PDK Plugin

**场景**: 开发 Agent-as-Plugin 范式的扩展插件

**代码入口**:
```cpp
// src/core/engine.cpp:125
engine.load_plugin("./libloop_agent.so");  // 动态加载 .so
engine.load_plugin("./libllama_engine.so");
```

**关键文件**:
- `src/modules/plugin/plugin_loader.cpp` — .so 加载（dlsym RTLD_LOCAL）
- `pdk/loop_agent/src/pdk_entry.cpp` — `pdk_plugin_info` + `pdk_register_tools`
- `pdk/llama_engine/src/pdk_entry.cpp` — 推理引擎插件
- `include/agenticdsl/contract/itool_registry.h` — 9 虚函数接口
- `include/agenticdsl/pdk/agent_loops/` — 3 种 Loop Agent

**适用人群**: 框架扩展者，需要自定义 Agent 行为

**典型例子**: `examples/pdk_chat_demo` — 完整的 PDK Plugin 演示

**PDK Plugin 清单**（已 ship 11 个）:
```
L2: shell_tools / fs_tools / provider_agent / budget_agent / session_agent / llama_engine / model_router
L4: loop_agent / g1_coding_assistant / temporal_agent / g3_knowledge_base
```

---

#### 入口 C：CLI/TUI（pdk_chat_demo）

**场景**: 开发多轮对话应用

**代码入口**:
```cpp
// examples/pdk_chat_demo/main.cpp:440
ChatSession session(&engine, bus, registry, agent_cfg, session_cfg);
session.chat("你好，帮我查一下天气");  // 用户输入
```

**关键文件**:
- `examples/pdk_chat_demo/main.cpp` — CLI 入口 + 信号处理
- `examples/pdk_chat_demo/chat_session.cpp` — 多轮对话编排器
- `examples/pdk_chat_demo/chat_session.h` — ChatSession API（chat / request_stop / request_model_switch）
- `pdk/loop_agent/src/pdk_entry.cpp` — loop/run 工具入口
- `include/agenticdsl/pdk/agent_loops/react_loop.h` — ReactLoop::run()

**适用人群**: 终端用户，使用现成的多轮对话应用

**CLI 标志**:
```bash
./pdk_chat_demo --mock                    # Mock LLM（默认）
./pdk_chat_demo --fork <node_id>         # 会话分支
./pdk_chat_demo --name <session_name>    # 会话命名
./pdk_chat_demo --system-prompt <text>   # 覆写系统提示
./pdk_chat_demo --append-system-prompt <text>  # 追加系统提示
```

---

### 1.2 入口到执行的 7 步主干

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                         用户视角入口到执行 7 步主干                                    │
└─────────────────────────────────────────────────────────────────────────────────────┘

     ┌─────────────────────────────────────────────────────────────────────┐
     │  步骤 1：用户输入                                                   │
     │  文件: examples/pdk_chat_demo/chat_session.cpp:chat()                │
     │  代码: session.chat("用户问题")                                       │
     └────────────────────────────┬────────────────────────────────────────┘
                                  │
                                  ▼
     ┌─────────────────────────────────────────────────────────────────────┐
     │  步骤 2：ChatSession 内部路由                                        │
     │  文件: examples/pdk_chat_demo/chat_session.cpp:244-266               │
     │  关键类: ChatSession::Impl                                           │
     │  行为: 发射 budget.checked 订阅 → 委托 engine.chat()                │
     └────────────────────────────┬────────────────────────────────────────┘
                                  │
                                  ▼
     ┌─────────────────────────────────────────────────────────────────────┐
     │  步骤 3：Loop Agent 选择                                             │
     │  文件: pdk/loop_agent/src/pdk_entry.cpp:pdk_register_tools()        │
     │  关键类: ReactLoop / PlanExecuteLoop / ForkJoinLoop                  │
     │  位置: include/agenticdsl/pdk/agent_loops/                          │
     │  选择依据: loop_type 参数（react/plan_execute/fork_join）              │
     └────────────────────────────┬────────────────────────────────────────┘
                                  │
                                  ▼
     ┌─────────────────────────────────────────────────────────────────────┐
     │  步骤 4：DSL 解析                                                   │
     │  文件: src/modules/parser/markdown_parser.cpp                        │
     │  关键类: MarkdownParser → ParsedGraph                                │
     │  输出: std::vector<Graph> full_graphs_                              │
     │  证据: src/core/engine.cpp:69 from_markdown()                        │
     └────────────────────────────┬────────────────────────────────────────┘
                                  │
                                  ▼
     ┌─────────────────────────────────────────────────────────────────────┐
     │  步骤 5：DAG 调度                                                   │
     │  文件: src/modules/scheduler/topo_scheduler.cpp                      │
     │  关键类: TopoScheduler                                              │
     │  方法: build_dag() → execute() → schedule()                        │
     │  并行: Taskflow N 路并行（默认 hardware_concurrency）                 │
     │  证据: src/modules/scheduler/topo_scheduler.cpp:100-150              │
     └────────────────────────────┬────────────────────────────────────────┘
                                  │
                                  ▼
     ┌─────────────────────────────────────────────────────────────────────┐
     │  步骤 6：节点执行                                                   │
     │  文件: src/modules/executor/node_executor.cpp:46                     │
     │  关键类: NodeExecutor                                                │
     │  方法: execute_node() → switch(node->type)                            │
     │  工具调用: dispatch_to_tool() → ToolCoordinator.execute()             │
     │  审批链: layer check → ApprovalHandler → audit → call               │
     │  证据: src/modules/executor/node_executor.cpp:213-498                │
     └────────────────────────────┬────────────────────────────────────────┘
                                  │
                                  ▼
     ┌─────────────────────────────────────────────────────────────────────┐
     │  步骤 7：LLM/工具执行 + Context 合并                               │
     │  LLM 调用: ILLMProvider.generate() + Decorator 链                   │
     │           (TracingDecorator → CostTrackingDecorator                    │
     │            → ComplianceDecorator → RateLimitDecorator → inner)       │
     │  工具调用: ToolRegistry.call_tool() → Plugin .so                     │
     │  Context 合并: NodeExecutor.process_output_keys()                      │
     │  事件发射: IInteractionBus.emit() → 27+ 主题                       │
     │  证据: src/modules/executor/node_executor.cpp:271                    │
     └────────────────────────────┬────────────────────────────────────────┘
                                  │
                                  ▼
                             输出结果

```

**关键 ADR 映射**:
- 步骤 3 选择: ADR-0021（PDK Loop Agent）
- 步骤 4 解析: ADR-0009（DSL 标准库）
- 步骤 5 调度: ADR-0019（IInteractionBus）
- 步骤 6 执行: ADR-0031（Execution Policy）+ ADR-0069（ToolCoordinator Hook）
- 步骤 7 LLM: ADR-0001（ILLMProvider）+ ADR-0042（Decorator 链）
- 步骤 7 工具: ADR-0004（ToolRegistry 安全）+ ADR-0023（ToolResult 标准）

---

## §二 核心数据流追踪

### 2.1 数据类别总览

| 数据类别 | 流向 | 生命周期 | 关键类 | 典型大小/频率 |
|---------|------|----------|--------|--------------|
| **用户输入** | 键盘 → ChatSession → Loop | 每轮 | `ChatSession::chat()` | <1KB，取决于用户输入长度 |
| **Context（短期记忆）** | DSL 内节点间传递 | 单次执行 | `LayeredContext`（5 层结构）| L1-L4 各 JSON，最大 ~50KB |
| **Session（3 层 + 4-Scope）** | UserSession → TaskSession → SubtaskSession | 多轮会话 | `SessionManager` + `UserSession` | JSONL 文件持久化，大小无上限 |
| **LLM Token 流** | ILLMProvider → Stream → Decorator | 每轮 LLM 调用 | `IGenerationStream` + `TracingDecorator` | 取决于模型输出，几十到几千 token |
| **Tool 调用** | dispatch_to_tool → ToolCoordinator → Plugin | 每工具调用 | `ToolCoordinator` + `IToolRegistry` | args JSON 通常 <10KB |
| **事件（IInteractionBus）** | emit() → 订阅者 | 异步 | `InMemoryBus`（27+ 主题）| 每个事件 <1KB，频率取决于应用 |
| **Trace/EventLog** | 节点执行时记录 | 执行期间 | `TraceExporter` + `EventLog` | 每节点 ~500 字节 |

#### Context 5 层结构（ADR-0008）

```
LayeredContext
├── L1 system:     系统级不变信息（session_id, user_id）
├── L2 user:       用户级共享上下文
├── L3 task:       当前任务上下文（由 DSL 填充）
├── L4 node:       当前节点输出（execute 返回值）
└── L5 raw:        原始未处理数据
```

**注意**: 此 L1-L5 编号与 OS 视角的 L0-L4 不同（差 1），容易混淆，详见 §四 混淆点 #5。

#### Session 3 层 + 4-Scope（ADR-0079）

```
UserSession（顶层，对应 4-Scope 的 Conversation Scope）
  └── TaskSession（任务级，对应 Attempt Scope）
        └── SubtaskSession（子任务级，对应 Step/Execution Scope）
```

4-Scope: Conversation / Attempt / Step / Execution

---

### 2.2 主数据流图（端到端）

```
┌──────────────────────────────────────────────────────────────────────────────────────┐
│                         HydraForge 端到端数据流                                      │
└──────────────────────────────────────────────────────────────────────────────────────┘

  ┌──────────────┐
  │  用户键盘输入  │  ← 步骤 1
  └──────┬───────┘
         │ "帮我查一下天气"
         ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │  ChatSession::chat() [chat_session.cpp:244]                        │
  │  ├─ 发射 budget.checked 订阅 [line 261-265]                         │
  │  ├─ 调用 DSLEngine::run() [line 310]                               │
  │  └─ 同步/异步双队列（steering / follow-up）[line 180-240]          │
  └──────┬─────────────────────────────────────────────────────────────┘
         │
         │ [同步]
         ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │  DSLEngine::run(UserSession&, message, LayeredContext)             │
  │  [engine.cpp:461-499]                                             │
  │  ├─ message → ctx["user_input"]                                  │
  │  ├─ TaskSession 创建/复用 [line 468-476]                          │
  │  └─ 委托 run_impl() [line 485]                                    │
  └──────┬─────────────────────────────────────────────────────────────┘
         │
         ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │  loop_agent Plugin [pdk/loop_agent/src/pdk_entry.cpp:60]            │
  │  ├─ pdk_register_tools("loop/run", handler)                       │
  │  └─ ReactLoop::run() / PlanExecuteLoop::run() / ForkJoinLoop::run()│
  │      [react_loop.h:80-138]                                          │
  └──────┬─────────────────────────────────────────────────────────────┘
         │
         │ LLM 调用（ILLMProvider.generate）
         ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │  Decorator 链 [engine.cpp:257-299]                                  │
  │  TracingDecorator → CostTrackingDecorator → ComplianceDecorator      │
  │   → RateLimitDecorator → inner (OrchestrationILLMProvider)           │
  │  同步/异步: 同步（generate 阻塞直到完成）                            │
  └──────┬─────────────────────────────────────────────────────────────┘
         │
         │ Token 流
         ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │  TopoScheduler::execute() [topo_scheduler.cpp:229]                  │
  │  ├─ build_dag() [line 100-150]                                    │
  │  ├─ Taskflow N 路并行调度                                          │
  │  └─ schedule() [line 200-280]                                      │
  └──────┬─────────────────────────────────────────────────────────────┘
         │
         │ 节点执行
         ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │  NodeExecutor::execute_node() [node_executor.cpp:46]                  │
  │  ├─ switch(node->type) → execute_xxx()                             │
  │  │   TOOL_CALL → execute_tool_call() [line 213]                     │
  │  │   DSL_CALL  → execute_dsl_node() [line 118]                      │
  │  │   YIELD     → execute_yield() [line 549]                        │
  │  │   GENERATE_SUBGRAPH → execute_generate_subgraph() [line 339]     │
  │  └─ 预算检查 [line 179-196]                                        │
  └──────┬─────────────────────────────────────────────────────────────┘
         │
         │ 工具调用 dispatch_to_tool()
         ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │  ToolCoordinator::execute() [tool_coordinator.cpp:349]                │
  │  ├─ layer check [line 360-380]                                      │
  │  ├─ ApprovalHandler::process_request() [line 390-420]              │
  │  │   （plan/agent/yolo 模式决策）                                  │
  │  ├─ audit 事件发射 [line 430-450]                                   │
  │  └─ registry.call_tool() → Plugin .so [line 460]                   │
  └──────┬─────────────────────────────────────────────────────────────┘
         │
         │ 同步（call_tool 阻塞直到 Plugin 返回）
         ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │  Plugin .so（shell_tools / fs_tools / llama_engine 等）              │
  │  ToolResult 返回 → ToolCoordinator → NodeExecutor                     │
  └──────┬─────────────────────────────────────────────────────────────┘
         │
         │ Context 合并 process_output_keys()
         ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │  IInteractionBus::emit() [inmemory_bus.cpp]                          │
  │  事件主题（27+）：llm.request / llm.response / tool.completed      │
  │   / domain.task.* / cognitive.task.* / mutation.* 等                │
  │  同步/异步: 异步（emit 入队，dispatch_thread 后台分发）               │
  └──────┬─────────────────────────────────────────────────────────────┘
         │
         ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │  TraceExporter / EventLog                                           │
  │  执行结果 → JSONL 持久化 / OTLP 导出                               │
  └──────┬─────────────────────────────────────────────────────────────┘
         │
         ▼
       输出

```

**同步/异步边界标注**:
- `ChatSession::chat()` → `DSLEngine::run()`: **同步**
- `ILLMProvider.generate()`: **同步**（阻塞直到 LLM 返回）
- `TopoScheduler::execute()`: **同步**（Taskflow 内部可并行）
- `ToolCoordinator::execute()`: **同步**（call_tool 阻塞）
- `IInteractionBus::emit()`: **异步**（入队即返，dispatch_thread 后台分发）

---

### 2.3 7 步详细追踪

#### 步骤 1：用户输入

**触发条件**: 用户在终端输入文字并回车

**关键类/方法**:
- `examples/pdk_chat_demo/chat_session.cpp:244` — `ChatSession::chat(const std::string& user_input)`
- `examples/pdk_chat_demo/chat_session.cpp:126` — `Impl::chat_impl()` 实际处理

**典型时序**:
```
用户按回车 
  → stdin 读取线程获取输入 
  → 判断是 steering (/) 还是 follow-up 
  → 入 steering_queue_ 或 follow_up_queue_
  → 同步路径直接调用 chat_impl()
```

**可能的失败模式**:
- stdin 读取失败（终端关闭）
- 输入队列满（steering_queue_ 默认容量 32，拒绝新输入 + stderr warning）

---

#### 步骤 2：ChatSession 内部路由

**触发条件**: `ChatSession::chat()` 被调用

**关键类/方法**:
- `examples/pdk_chat_demo/chat_session.cpp:310` — `chat_impl(user_input, token)`
- `examples/pdk_chat_demo/chat_session.cpp:261-265` — budget 订阅设置

**典型时序**:
```
chat_impl()
  ├─ ctx["user_input"] = user_input
  ├─ emit("chat.user.input", {...}) [如果 bus 可用]
  └─ 调用 loop/run 工具
       → DSLEngine::run(UserSession&, message, LayeredContext)
```

**可能的失败模式**:
- engine 为空（未初始化）
- session 已关闭

---

#### 步骤 3：Loop Agent 选择

**触发条件**: `loop/run` 工具被调用

**关键类/方法**:
- `pdk/loop_agent/src/pdk_entry.cpp:60` — `register_tool_function("loop/run", ...)`
- `include/agenticdsl/pdk/agent_loops/react_loop.h:80` — `ReactLoop::run()`
- `include/agenticdsl/pdk/agent_loops/plan_execute_loop.h` — `PlanExecuteLoop::run()`
- `include/agenticdsl/pdk/agent_loops/fork_join_loop.h` — `ForkJoinLoop::run()`

**Loop 选择依据**:
| loop_type 参数 | 选用的 Loop | 状态机 |
|--------------|-------------|--------|
| `react` | ReactLoop | Thinking → Acting → Observing → Done |
| `plan_execute` | PlanExecuteLoop | Planning → Executing → Verifying → Done/Retry |
| `fork_join` | ForkJoinLoop | Forking → Executing → Joining → Done |

**典型时序**:
```
loop/run tool handler
  └─ LoopDispatcher<LoopType>::Type
       └─ switch(loop_type)
            ├─ ReactLoop: SimpleCognitiveOrchestrator 单轮
            ├─ PlanExecuteLoop: plan → execute → verify 3 阶段
            └─ ForkJoinLoop: DomainWorkerPool 并行分支
```

**可能的失败模式**:
- engine 为空（ReactLoop line 95-101 有 null 检查）
- token 已请求停止（ReactLoop line 87-93 提前返回）

---

#### 步骤 4：DSL 解析

**触发条件**: `DSLEngine::from_markdown()` 被调用

**关键类/方法**:
- `src/core/engine.cpp:69` — `DSLEngine::from_markdown(const std::string& markdown)`
- `src/modules/parser/markdown_parser.cpp` — `MarkdownParser::parse()`
- `src/core/types/node.h` — `ParsedGraph` 结构

**典型时序**:
```
from_markdown(markdown)
  └─ MarkdownParser.parse(markdown)
       ├─ 提取 metadata (version / budget / nodes)
       ├─ 解析每个节点 → Node 子类实例
       └─ 返回 std::vector<ParsedGraph>
```

**节点类型**（DSL 规范）:
- `start` / `end` — 流程控制
- `assign` — 变量赋值
- `dsl_call` — 调用子图
- `tool_call` — 调用工具
- `generate_subgraph` — 动态生成 DSL（⚠️ 断链，详见 §四 混淆点）
- `yield` — 暂停等待外部输入
- `fork` / `join` — 并行分支

**可能的失败模式**:
- Markdown 格式错误（parser 抛异常）
- 节点类型未知（`execute_node` default 抛 `runtime_error`）

---

#### 步骤 5：DAG 调度

**触发条件**: `TopoScheduler::execute()` 被调用

**关键类/方法**:
- `src/modules/scheduler/topo_scheduler.cpp:100` — `build_dag()`
- `src/modules/scheduler/topo_scheduler.cpp:229` — `execute()` 主循环
- `src/modules/scheduler/topo_scheduler.cpp:200` — `schedule()`

**典型时序**:
```
execute(ctx)
  ├─ build_dag()
  │    ├─ 计算 in_degree（入度）
  │    ├─ 构建 reverse_edges（反向边）
  │    └─ seed_initial_ready_queue()（入度为 0 的节点入队）
  ├─ while (!ready_queue.empty() || !executing.empty())
  │    ├─ 消费 ready_queue 中的节点
  │    ├─ Taskflow::emplace() 添加任务
  │    └─ taskflow.run() 执行
  └─ 返回 ExecutionResult
```

**并行调度**:
- 默认 worker 数 = `hardware_concurrency`（line 229: `num_workers == 0` 时退化）
- 可通过 `TopoScheduler::Config::num_workers` 注入（`test_execute_parallel.cpp`）

**可能的失败模式**:
- DAG 有环（`build_dag()` 检测到循环依赖）
- 预算耗尽（`try_consume_node()` 返回 false）
- 所有节点等待但无人完成（死锁，理论上不可能因为 DAG 保证）

---

#### 步骤 6：节点执行

**触发条件**: TopoScheduler 从 ready_queue 取出节点并执行

**关键类/方法**:
- `src/modules/executor/node_executor.cpp:46` — `execute_node(Node*, Context, BudgetChecker)`
- `src/modules/executor/node_executor.cpp:213` — `execute_tool_call()`
- `src/modules/executor/node_executor.cpp:447` — `dispatch_to_tool()`
- `src/common/tools/tool_coordinator.cpp:349` — `ToolCoordinator::execute()`

**典型时序**:
```
execute_node(node, ctx)
  ├─ check_permissions(node->permissions, node->path)
  ├─ switch(node->type)
  │    └─ case TOOL_CALL: execute_tool_call()
  │         ├─ 渲染参数模板 InjaTemplateRenderer
  │         ├─ dispatch_to_tool(tool_name, args)
  │         │    ├─ tool_coordinator_->execute(meta, ctx, args, token)
  │         │    │    ├─ layer check [tool_coordinator.cpp:360-380]
  │         │    │    ├─ approval_handler_->process_request() [line 390-420]
  │         │    │    ├─ emit audit.invoked [line 430]
  │         │    │    └─ registry.call_tool() → Plugin
  │         │    └─ 返回 ToolResult
  │         ├─ handle_tool_errors()（可选重试）
  │         └─ process_output_keys(ctx, output_keys, result)
  └─ 返回更新后的 Context
```

**layer check 逻辑**（`tool_coordinator.cpp:360-380`）:
```
Workflow:   所有工具允许
Thinking:   ReadOnly + WriteFile
Cognitive:  ReadOnly（不允许写文件）
```

**ApprovalHandler 决策**（`tool_coordinator.cpp:390-420`）:
| Policy Mode | requires_approval() | 行为 |
|------------|---------------------|------|
| Plan | `ApprovalPolicy::plan` | 仅 plan 阶段审批 |
| Agent | `ApprovalPolicy::agent` | 仅 agent 阶段审批 |
| Yolo | 始终 false | 从不审批（⚠️ 危险）|

**可能的失败模式**:
- 工具不存在（`has_tool()` 返回 false，抛 `runtime_error`）
- 审批拒绝（抛 `runtime_error("Tool ... denied by execution policy")`）
- 预算耗尽（`try_consume_node()` 返回 false，跳过节点）

---

#### 步骤 7：LLM/工具执行 + Context 合并

**触发条件**: `ILLMProvider::generate()` 或 `IToolRegistry::call_tool()` 被调用

**关键类/方法**:
- `src/common/llm/tracing_decorator.cpp` — TracingDecorator 发射 `llm.request` / `llm.response`
- `src/common/llm/cost_tracking_decorator.cpp` — 预算扣费
- `src/modules/executor/node_executor.cpp:271` — `bus_->emit(EventBuilder("tool.completed", tool_result).build())`
- `src/modules/executor/node_executor.cpp:265` — `process_output_keys()`

**Decorator 链顺序**（`engine.cpp:257-299`）:
```
请求 ──→ TracingDecorator ──→ CostTrackingDecorator ──→ ComplianceDecorator
     ──→ RateLimitDecorator ──→ OrchestrationILLMProvider（实际 LLM）
响应 ←────────────────────────────────────────────────────────────────
```

**Context 合并**（`node_executor.cpp:265`）:
```cpp
// process_output_keys() 将工具返回值写入 Context
for (const auto& key : node->output_keys) {
    ctx[key] = tool_result.data;
}
```

**事件发射**（27+ 主题，详见 ADR-0068 Appendix A）:
- `llm.request` / `llm.response` — LLM 调用
- `tool.completed` / `tool.audit.denied` — 工具调用
- `cognitive.task.started` / `cognitive.task.completed` — 认知任务
- `domain.task.*` — 领域任务
- `session.persisted` — 会话持久化

**可能的失败模式**:
- LLM 返回错误（`ToolResult.error_code` 设置，`ok=false`）
- EventBus dispatch 线程异常（异步，异常不穿透）

---

## §三 典型使用场景追踪

### 场景 A：单轮 ReAct（用户问"2+2=?"）

**用户输入**: "2+2=?"

**触发的入口**: 入口 C（CLI/TUI）→ `ChatSession::chat()`

**走过的代码路径**:
```
ChatSession::chat("2+2=?")
  └─ DSLEngine::run(UserSession&, message, LayeredContext)
       └─ loop/run 工具（loop_agent Plugin）
            └─ ReactLoop::run(prompt, ctx)
                 └─ SimpleCognitiveOrchestrator::process()
                      ├─ LLM generate("2+2=?")
                      │    └─ Decorator 链 → llama.cpp
                      └─ 解析 JSON {tool_call: "math/add", args: {a:2, b:2}}
                           └─ dispatch_to_tool("math/add", {a:2, b:2})
                                └─ ToolCoordinator → math_tools Plugin
                                     └─ 返回 4
```

**产生的事件/产物**:
- `llm.request` / `llm.response`
- `tool.completed`
- `ctx["answer"] = 4`

**关键决策点**:
- Loop 选择：`react`（单轮）
- 工具选择：math/add 工具
- 审批流：无（math/add 是 ReadOnly 类）

---

### 场景 B：多轮对话 + 会话分支（`--fork`）

**用户输入**: `--fork sess_abc123 node_5`

**触发的入口**: 入口 C → `ChatSession::load_from_disk()` + `SessionManager::fork_session()`

**走过的代码路径**:
```
main.cpp: 解析 --fork <session_id> <node_id>
  └─ session_manager.load(session_id) [session_manager.cpp]
       └─ session_store.extract(session_id, node_id) [session_store.cpp]
            ├─ 找到 session_id 对应的 JSONL
            ├─ 找到 node_id 对应的 checkpoint
            └─ 创建新 session（分支）
```

**产生的事件/产物**:
- 新 session_id 生成
- `session.persisted` 事件
- 分支 cursor 指向 node_5

**关键决策点**:
- `--fork` 时验证 session_id 存在
- node_id 必须是已执行过的节点
- 新 session 继承原 session 的完整历史

---

### 场景 C：审批流（用户调用 fs/write）

**用户输入**: "帮我写一个文件"

**触发的入口**: 入口 C → `loop/run` → `fs/write` 工具

**走过的代码路径**:
```
execute_tool_call(node, ctx)
  └─ dispatch_to_tool("fs/write", args)
       └─ ToolCoordinator::execute()
            ├─ layer check: WritingFile 需要 Thinking/Workflow 权限
            │   └─ 用户当前是 Cognitive 模式 → 需要审批
            ├─ ApprovalHandler::process_request()
            │   ├─ policy->requires_approval(metadata, ctx) → true
            │   └─ 阻塞等待用户确认（或 stdin 读）
            │        ↓ 用户输入 "y"
            ├─ audit 事件发射: tool.audit.invoked
            └─ registry.call_tool("fs/write", args)
                 └─ fs_tools Plugin
```

**产生的事件/产物**:
- `tool.audit.invoked` / `tool.audit.denied` 或 `tool.audit.completed`
- 文件系统写入

**关键决策点**:
- Policy Mode 决定是否需要审批
- `ApprovalPolicy::plan` → 仅 plan 阶段审批
- `ApprovalPolicy::agent` → 仅 agent 阶段审批
- `ApprovalPolicy::yolo` → 从不审批（⚠️ 危险）

---

### 场景 D：Skill 隔离执行（`pdk/loop_agent` 启动子进程）

**用户输入**: "执行一个 SKILL.md 定义的技能"

**触发的入口**: 入口 B（Plugin 加载）→ `SkillInterpreter::run()`

**走过的代码路径**:
```
SkillInterpreter::run(skill_path, args)
  ├─ 解析 SKILL.md 获取 signature / permissions
  ├─ 检查隔离要求（requires_isolation == true？）
  │    └─ posix_spawn() → 子进程执行
  │         ├─ execve("/proc/self/exe", ["--skill-child", skill_path])
  │         └─ seccomp(BPF) 限制系统调用
  └─ 父进程等待 IPC 管道返回
       └─ SkillResult 通过 pipe 返回
```

**产生的事件/产物**:
- `skill.started` / `skill.completed`
- SkillInterpreter 的 V1 实现（`pdk/loop_agent` 暂未使用真实 SkillInterpreter）

**关键决策点**:
- `requires_isolation` 判断是否需要进程隔离
- posix_spawn vs in-process 执行
- seccomp BPF 限制哪些系统调用

**⚠️ 当前状态**: SkillInterpreter V1 已 ship（ADR-0066），但 loop_agent 当前使用 ReactLoop 并非真实 SKILL.md 解释执行

---

### 场景 E：多智能体编排（CognitiveWorker + DomainWorkerPool 并行）

**用户输入**: "帮我写代码并测试"

**触发的入口**: 入口 A（嵌入式）或 B（Plugin）

**走过的代码路径**:
```
CognitiveWorker::submit_task(DomainTask{code_gen})
  └─ DomainWorkerPool.submit(task)
       ├─ 任务入 FIFO 队列
       └─ jthread worker 消费
            └─ fork_join_loop 调用 code_gen 工具

CognitiveWorker::submit_task(DomainTask{test_run})
  └─ DomainWorkerPool.submit(task)
       ├─ 任务入 FIFO 队列
       └─ jthread worker 消费
            └─ fork_join_loop 调用 test 工具
```

**CognitiveWorker vs DomainWorkerPool**:
| 组件 | 角色 | 关键方法 |
|------|------|----------|
| `CognitiveWorker` | 编排者（理解意图 → 分解任务）| `submit_task()` / `get_result()` |
| `DomainWorkerPool` | 执行者（提供工具能力）| `submit_task()` / `register_domain_handler()` |

**产生的事件/产物**:
- `cognitive.task.submitted` / `cognitive.task.completed`
- `domain.task.started` / `domain.task.completed`
- 最终测试报告

**关键决策点**:
- CognitiveWorker 决定任务分解策略
- DomainWorkerPool 并行度（默认 hardware_concurrency）
- 任务失败时的重试策略

---

## §四 混淆/不清点（诚实审计）

### 混淆点 1：L0-L4（OS 视角）vs L1-L5（编排视角）

**现象**: 新 contributor 读文档时发现两套"5 层"说法，容易混淆

**代码证据**:
- OS 视角（`docs/specs/architecture.md:65-149`）:
  ```
  L0: Runtime Core（DSLEngine / NodeExecutor）
  L1: OS Services（IInteractionBus / ToolRegistry）
  L2: Plugin Tools（shell_tools / fs_tools）
  L3: PDK Contract（IToolRegistry / ITemporalClient）
  L4: Agent App（Loop Agent / Temporal Agent）
  ```
- 编排视角（`docs/architecture/agent-orchestration-architecture-2026-08.md:14-49`）:
  ```
  编排层 → 行为编排层 → 认知执行层 → 领域执行层 → 可观测层
  （对应 L5 → L4 → L3 → L2 → L1？）
  ```

**文档证据**:
- `docs/specs/architecture.md` §2.1 五层抽象（L0-L4）
- `docs/architecture/agent-orchestration-architecture-2026-08.md` §一 编排全景（5 层模型）

**解释**: OS 视角是静态架构分层（L0 核心 → L4 应用），编排视角是运行时执行分层（从编排到可观测）。两者都叫"5 层"但编号体系不同。

**建议理解方式**: 
- 看架构文档时，先确认是 OS 视角还是编排视角
- OS 视角：L0 是基础服务，越高层越接近用户
- 编排视角：从上到下是数据流方向

---

### 混淆点 2：MCP 一词多义

**现象**: "MCP" 在项目中出现时，可能指：
1. **外部标准 MCP**（Model Context Protocol，类似 Anthropic 的标准）
2. **LayeredContext 等自研协议**（Markdown Context Protocol？）
3. **ADR-0076 DSL Engine as MCP Server**（HydraForge 作为 MCP Server）

**代码证据**:
- `docs/adr/adr-0076-dsl-engine-mcp-server.md` — DSL Engine as MCP Server（Proposed）
- `docs/specs/architecture.md:51` — MCP 作为 SOTA 对比表格中的外部框架

**文档证据**:
- ADR-0076 描述："DSL Engine as MCP Server 控制面（D1 stdio+HTTP+SSE + D2 静态 token）"
- `capability-application-map-2026-08.md` G9："ADR-0076 DSL Engine as MCP Server → B5 DSL-as-MCP-tool"

**解释**: "MCP" 在此项目中通常指 ADR-0076（HydraForge 作为 MCP Server），而非外部标准协议。但文档中可能出现歧义。

**建议理解方式**: 
- 看到"MCP"先判断上下文，是 HydraForge 的 MCP Server 特性还是外部标准
- ADR-0076 目前状态是 🔍 Proposed，未 ship

---

### 混淆点 3：SKILL 隔离执行级别差异

**现象**: SKILL.md 可以用 posix_spawn / Wasm / in-process 三种方式执行，文档描述不一致

**代码证据**:
- `include/agenticdsl/skill/skill_interpreter.h:50-90` — SkillInterpreter 定义
- `pdk/loop_agent/src/pdk_entry.cpp` — 当前 loop_agent 用 ReactLoop 并非真实 SKILL

**文档证据**:
- `docs/specs/architecture.md:385-443` — Form::Skill 描述
- ADR-0066（SkillInterpreter 架构）状态: 🟡 Partial（V1 ship，V2 deferred）

**解释**: 
- V1：posix_spawn 进程隔离
- V2（deferred）：Wasm 沙箱
- in-process：直接调用（无隔离，不推荐）

**建议理解方式**:
- 当前生产代码用 ReactLoop/PlanExecuteLoop/ForkJoinLoop
- SkillInterpreter V1 已 ship，但 loop_agent 尚未集成真实 SKILL 执行
- 隔离级别：posix_spawn > in-process（无隔离）

---

### 混淆点 4：ChatSession "直连 LLM" 分支已删除但文档未同步

**现象**: 旧文档描述 ChatSession 可以直接调用 LLM，但代码中该路径已删除

**代码证据**:
- `openspec/changes/archive/2026-08-07-loop-agent-bypass/` — fix-loop-agent-bypass change
- 代码中 loop_agent 通过 `call_tool("loop/run")` 而非直接调用 LLM

**文档证据**:
- `examples/pdk_chat_demo/DESIGN.md` — 可能仍描述旧的直连路径

**解释**: 修复后 loop_agent 统一走 `call_tool("loop/run")`，不再有直连 LLM 的旁路

**建议理解方式**:
- 看代码：`pdk/loop_agent/src/pdk_entry.cpp:60` 的 `register_tool_function("loop/run", ...)`
- 实际路径：ChatSession → loop/run tool → Loop 类 → LLM
- 没有从 ChatSession 直接到 LLM 的调用

---

### 混淆点 5：LayeredContext L1-L5（5 层）vs OS L0-L4（5 层）编号冲突

**现象**: LayeredContext 的 L1-L5 和 OS 架构的 L0-L4 都叫"5 层"，但编号差 1

**代码证据**:
- `include/agenticdsl/types/layered_context.h` — L1 system / L2 user / L3 task / L4 node / L5 raw
- `docs/specs/architecture.md:145-149` — L0 Runtime Core / L1 OS Services / L2 Plugin Tools / L3 PDK Contract / L4 Agent App

**文档证据**:
- ADR-0008（LayeredContext）定义 5 层
- `docs/specs/architecture.md` 定义 OS 5 层

**解释**: 这是两个独立的 5 层体系，编号差 1：
- LayeredContext：L1 = 最内层/系统级
- OS 架构：L0 = 最内层/核心

**建议理解方式**: LayeredContext 的 L1 相当于 OS 架构的 L0，实际是同一套东西的不同视角描述

---

### 混淆点 6：ToolCoordinator vs IToolRegistry 职责边界

**现象**: 两者都负责工具调用，但职责不清

**代码证据**:
- `src/common/tools/tool_coordinator.cpp:349` — `ToolCoordinator::execute(meta, ctx, args, token)`
- `include/agenticdsl/contract/itool_registry.h` — 9 虚函数接口

**ToolCoordinator 职责**:
1. layer check（检查是否允许某 layer 的工具）
2. ApprovalHandler 调用（审批决策）
3. audit 事件发射（记录调用）
4. 调用 ToolRegistry

**IToolRegistry 职责**:
1. 工具注册（register_tool_function）
2. 工具查询（has_tool）
3. 实际调用（call_tool）

**解释**: ToolCoordinator 是 Middleware，IToolRegistry 是基础设施。ToolCoordinator 在 IToolRegistry 之上加了一层政策检查。

**建议理解方式**:
- 需要加 policy/approval/audit → 用 ToolCoordinator
- 需要基础注册/调用 → 用 IToolRegistry

---

### 混淆点 7：Plan/Agent/Yolo 三模式 vs 实际审批流

**现象**: 三种执行模式在代码中如何对应实际审批决策

**代码证据**:
- `src/common/policy/execution_policy.h` — PlanPolicy / AgentPolicy / YoloPolicy
- `src/common/policy/approval_handler.cpp` — process_request 实现

**三模式含义**:
| 模式 | requires_approval() | 说明 |
|------|-------------------|------|
| Plan | `ApprovalPolicy::plan` | 仅 plan 阶段审批 |
| Agent | `ApprovalPolicy::agent` | 仅 agent 阶段审批 |
| Yolo | 始终 false | 从不审批（⚠️ 危险）|

**解释**: Yolo 模式跳过了所有审批流程，直接执行工具，生产环境慎用。

---

### 混淆点 8：CognitiveWorker / DomainWorkerPool / ForkJoinLoop 三者都是"并行"但语义不同

**现象**: 三者都涉及并行，但含义不同，容易混淆

**代码证据**:
- `include/agenticdsl/cognitive/cognitive_worker.h` — CognitiveWorker
- `include/agenticdsl/cognitive/domain_worker_pool.h` — DomainWorkerPool
- `include/agenticdsl/pdk/agent_loops/fork_join_loop.h` — ForkJoinLoop

**三者区别**:
| 组件 | 并行语义 | 使用场景 |
|------|---------|---------|
| `CognitiveWorker` | 多 agent 隔离执行（jthread + stop_token）| 多用户/多会话隔离 |
| `DomainWorkerPool` | 领域任务并行消费（FIFO 队列 + jthread）| 代码生成 + 测试并行 |
| `ForkJoinLoop` | 分支并行执行（fork → parallel → join）| 多工具并行调用 |

**建议理解方式**: 
- 需要多用户隔离 → CognitiveWorker
- 需要任务队列并行 → DomainWorkerPool
- 需要分支并行 → ForkJoinLoop

---

## §五 架构缺失（诚实清单）

### G1: compact 破坏性重写

**缺失内容**: SessionManager 的 compact 方法是 in-place 破坏性重写，不是真正的 append-only 压缩

**影响的用户场景**: A2（多轮对话 + 会话分支）— compact 后历史丢失

**跟踪的 ADR**: ADR-0079 v1.2（Pending）

**当前状态**: 🔒 Blocked（待架构组决策）

---

### G2: EventLog query API 自动化校验

**缺失内容**: EventLog.query() 缺乏自动化测试覆盖

**影响的用户场景**: B3（真实分布式追踪）— query API 行为回归

**跟踪的 ADR**: ADR-0080（已 Approved，但 query API 测试覆盖不足）

**当前状态**: 🔓 Open

---

### G3: AgentWorker 完整实现 + spawn_agent + YAML

**缺失内容**: IAgentRegistry 骨架已 ship，但完整 AgentWorker + YAML 配置 + spawn_agent 未实现

**影响的用户场景**: B2（跨进程多 agent 协作）

**跟踪的 ADR**: ADR-0082（V1 骨架已 ship，完整实现 T3+T4 未完成）

**当前状态**: 🔓 Open（前置 IAgentRegistry ✅ 已 ship）

---

### G4: Agent↔Agent stream 模式

**缺失内容**: IAgentComposition 的 stream 模式是占位符，未实现

**影响的用户场景**: B4（Streaming Agent）

**跟踪的 ADR**: ADR-0060（stream 占位，详见 `agent_composition.h:20`）

**当前状态**: 🔓 Open（需 T6）

---

### G5: Plugin per-agent 隔离

**缺失内容**: 当前 ToolRegistry 是 per-engine，不是 per-agent，多租户场景下 agent 间无隔离

**影响的用户场景**: B1（Marketplace Agent 部署）

**跟踪的 ADR**: ADR-0022 + ADR-0082

**当前状态**: 🟡 Partial（per-engine ✅，per-agent ❌）

---

### G6: Agent hook loop 集成

**缺失内容**: IAgentHookRegistry 骨架已 ship（ADR-0081），但与 Loop 的集成未完成

**影响的用户场景**: 全部 B/C 类应用的可观测性

**跟踪的 ADR**: ADR-0081（V1 骨架已 ship，loop 集成 T6 未完成）

**当前状态**: 🔓 Open（前置 IAgentHookRegistry ✅ 已 ship）

---

### G7: Structured concurrency (scope tree)

**缺失内容**: C++20 scope tree 未实现，嵌套取消依赖手动管理

**影响的用户场景**: 提升所有 B/C 类应用的可靠性

**跟踪的 ADR**: 提案中（无 ADR 编号）

**当前状态**: 🟡 Partial（协作取消已 ship，scope tree 未实现）

---

### G8: OTel 真实 OTLP 客户端

**缺失内容**: 当前是 NoopSink，真实 OTLP 客户端未实现

**影响的用户场景**: B3（真实分布式追踪）

**跟踪的 ADR**: ADR-0080（EventLog 已 ship，OTLP 导出待实现）

**当前状态**: 🔓 Open（需 T2）

---

### G9: ADR-0076 DSL Engine as MCP Server

**缺失内容**: DSL Engine 作为 MCP Server 的完整实现（stdio/HTTP/SSE transport + capability 暴露）

**影响的用户场景**: B5（DSL-as-MCP-tool）

**跟踪的 ADR**: ADR-0076（🔍 Proposed）

**当前状态**: 🔓 Open（gated by active-status.md §四）

---

## §六 相关文档索引

### 架构文档

| 文档 | 路径 | 说明 |
|------|------|------|
| AgenticOS 五层架构规范 | `docs/specs/architecture.md` | L0-L4 + R1-R5 硬约束 |
| 5 层编排模型 | `docs/architecture/agent-orchestration-architecture-2026-08.md` | 编排层/认知执行层/领域执行层 |
| 多领域智能体架构 | `docs/architecture/multi-domain-agent-architecture.md` | Cognitive/Domain 分层 |
| 能力应用地图 | `docs/architecture/capability-application-map-2026-08.md` | 31 项 ship + 9 项 gap |
| 五层缺失分析 | `docs/architecture/layer-based-missing-capabilities-analysis.md` | 5 层缺失能力 |
| 自进化架构 | `docs/architecture/self-evolution-architecture-2026-08.md` | GEPA/MCTS/蒸馏 |

### ADR（按流程节点）

| ADR | 议题 | 状态 |
|-----|------|------|
| ADR-0001 | ILLMProvider 流式接口 | ✅ Approved |
| ADR-0004 | ToolRegistry 安全模型 V2 | ✅ Approved |
| ADR-0008 | LayeredContext 5 层 | ✅ Approved |
| ADR-0009 | DSL 标准库 | ✅ Approved |
| ADR-0019 | IInteractionBus MVP | 🟡 Partial |
| ADR-0020 | CognitiveWorker/DomainWorkerPool | ✅ Approved |
| ADR-0021 | PDK Loop Agent | ✅ Approved |
| ADR-0022 | Plugin 加载 | ✅ Approved |
| ADR-0023 | ToolResult 标准 | ✅ Approved |
| ADR-0031 | Execution Policy | 🟡 Partial |
| ADR-0033 | Session 3 层 | ✅ Approved |
| ADR-0066 | SkillInterpreter 架构 | 🟡 Partial |
| ADR-0068 | 事件发射契约 | ✅ Approved |
| ADR-0069 | ToolCoordinator Hook | 🟡 Partial |
| ADR-0076 | DSL Engine as MCP Server | 🔍 Proposed |
| ADR-0079 | Session 4-Scope | ✅ Approved |
| ADR-0080 | AppendOnlyEventLog | ✅ Approved |
| ADR-0081 | Agent Hook 契约 | ✅ Approved |
| ADR-0082 | IAgentRegistry | ✅ Approved |
| ADR-0083 | IEvaluator 契约 | ✅ Approved |
| ADR-0084 | MutationGovernor | ✅ Approved |

### 规范文档

| 文档 | 路径 | 说明 |
|------|------|------|
| DSL v3.10 规范 | `docs/specs/dsl.md` | 节点类型 / YAML 语法 |
| STDLIB v3.10 | `docs/specs/stdlib-v3.10.md` | 标准库工具 |
| Memory v3.10 | `docs/specs/memory-v3.10.md` | 上下文记忆 |
| Layer0 运行时 | `docs/specs/layer0.md` | L0 核心行为 |

### 设计文档

| 文档 | 路径 | 说明 |
|------|------|------|
| PDK Chat Demo | `examples/pdk_chat_demo/DESIGN.md` | 完整 demo 设计 |
| PKM Temporal Demo | `examples/pkm_temporal_demo/DESIGN.md` | Temporal Agent PoC |
| PDK 设计 | `docs/adr/adr-0021-pdk-design.md` | Plugin 开发套件 |

---

## 附录 A：术语表

### 核心概念

| 术语 | 定义 | 证据位置 |
|------|------|---------|
| **Agent** | 可执行的工作单元，通过 IToolRegistry 调用工具，可组合形成复杂工作流 | `docs/specs/architecture.md:18` |
| **Plugin** | .so/.dll/.wasm 形态的扩展，通过 pdk_plugin_info 导出符号 | `docs/specs/architecture.md:249-310` |
| **Loop** | Agent 的执行循环模式（React/PlanExecute/ForkJoin）| `include/agenticdsl/pdk/agent_loops/` |
| **Cognitive Worker** | 编排者角色，负责意图理解 → 任务分解 → DSL 生成 | `include/agenticdsl/cognitive/cognitive_worker.h` |
| **Domain Worker** | 执行者角色，负责提供领域工具能力 | `include/agenticdsl/cognitive/domain_worker_pool.h` |
| **Hook** | 拦截点，在工具/LLM 调用前后执行自定义逻辑 | `include/agenticdsl/contract/itool_hook_registry.h` |
| **Bus** | IInteractionBus，事件总线，27+ 主题 | `include/agenticdsl/contract/iinteraction_bus.h` |
| **Trace** | 执行轨迹，记录每个节点开始/结束/输入/输出 | `include/agenticdsl/types/trace_record.h` |

### 缩略语

| 缩写 | 全称 | 说明 |
|------|------|------|
| DSL | Domain Specific Language | 工作流定义语言（Markdown 格式）|
| PDK | Plugin Development Kit | 插件开发套件 |
| MCP | Model Context Protocol | HydraForge 的 DSL Engine as MCP Server（ADR-0076）|
| SKILL | Skill Markdown | 声明式技能工作流（隔离执行）|
| OS | Operating System | AgenticOS，HydraForge 的自称 |
| DAG | Directed Acyclic Graph | 有向无环图，工作流拓扑 |
| ReAct | Reasoning + Acting | 单轮工具调用范式 |

---

## 附录 B：验证命令

### 验证 31 项 ship 能力

```bash
# L0 (5 项): 引擎核心
ls src/core/engine.cpp  # DSLEngine::from_markdown
ls src/modules/scheduler/topo_scheduler.cpp  # TopoScheduler::execute
ls src/modules/executor/node_executor.cpp  # NodeExecutor::execute_node
ls include/agenticdsl/types/layered_context.h  # LayeredContext
ls src/core/context_compactor.cpp  # ContextCompactor

# L1 (5 项): 智能体循环
ls include/agenticdsl/pdk/agent_loops/react_loop.h
ls include/agenticdsl/pdk/agent_loops/plan_execute_loop.h
ls include/agenticdsl/pdk/agent_loops/fork_join_loop.h
ls include/agenticdsl/cognitive/cognitive_worker.h
ls include/agenticdsl/cognitive/domain_worker_pool.h

# L2 (4 项): 工具层
ls include/agenticdsl/contract/itool_registry.h
ls src/common/tools/secure_tool_registry.cpp
ls include/agenticdsl/contract/itool_hook_registry.h
ls include/agenticdsl/pdk/tool_macros.h

# L3 (5 项): 插件层
ls src/modules/plugin/plugin_loader.cpp
ls include/agenticdsl/contract/iagent_registry.h
ls include/agenticdsl/contract/iagent_hook_registry.h
ls include/agenticdsl/contract/iagent_composition.h
ls include/agenticdsl/pdk/agent_macros.h

# L4 (12 项): 可观测+治理
ls src/core/event_log.cpp
ls src/core/session_manager.cpp
ls include/agenticdsl/types/tool_result.h
ls include/agenticdsl/testing/behavioral_regression.h
ls include/agenticdsl/cognitive/skill_compiler.h
ls include/agenticdsl/ir/trajectory_ir.h
ls include/agenticdsl/cognitive/gepa_loop.h
ls include/agenticdsl/cognitive/mcts_workflow_search.h
```

### 验证 9 项 open gap

```bash
# G1: compact 仍是 in-place（检查 rename vs O_TRUNC）
grep -n "::rename\|O_TRUNC" src/core/session_manager.cpp

# G3: AgentWorker 完整实现
grep -n "spawn_agent\|YAML.*agent" src/core/agent_registry.cpp  # 应返回空

# G4: stream 模式
grep -n "stream.*throw\|stream.*Placeholder" src/modules/cognitive/agent_composition.cpp

# G6: Agent hook loop 集成
grep -n "IAgentHookRegistry.*apply" src/modules/cognitive/cognitive_worker.cpp

# G8: OTel 真客户端
grep -n "OTLP\|OtlpHttp" src/common/observability/  # 应返回空

# G9: MCP Server
grep -l "MCP\|mcp_server" src/ include/  # 应返回空
```

### 验证 ctest 全量通过

```bash
cd build && ctest --output-on-failure 2>&1 | tail -20
# 预期: 185/185 PASS, 0 failures
```

### 验证文档完整性

```bash
# 检查 capability-application-map §一 31 项
grep -cE "^\| \*\*[0-9]+\*\* " docs/architecture/capability-application-map-2026-08.md

# 检查 9 项 open gap
grep -cE "^### Gap" docs/architecture/capability-application-map-2026-08.md

# 检查 ADR 状态
python3 tools/adr_lint.py 2>&1 | tail -10
```

---

## 变更记录

| 日期 | 版本 | 变更 |
|------|------|------|
| 2026-09-04 | v0.1 | 初始版本草案 |

---

**备注**: 本文档是快照文档，描述 HydraForge 当前架构状态。所有架构决策应以对应 ADR 文件（`docs/adr/*.md`）的 `## 状态` 字段为准。
