# HydraForge 混淆点与架构缺失清单

**生成日期**: 2026-09-04
**最后验证**: 2026-09-04（v0.2 — 同步源文档 `docs/guides/usage-flow-architecture.md` v0.4 修正状态：混淆点 4 标记已解决）
**作者**: Architecture Working Group
**状态**: 🔍 Proposed（草案 v0.2）

**关联文档**:
- `docs/guides/usage-flow-architecture.md` — 用户视角入口架构文档（本文件的问题来源）
- `docs/specs/architecture.md` — AgenticOS 五层架构规范（L0~L4 + R1~R5）
- `docs/architecture/capability-application-map-2026-08.md` — 31 项已 ship 能力 + 9 项 open gap

---

## §〇 TL;DR

本文件是 `docs/guides/usage-flow-architecture.md` 拆分出的**问题清单**，包含：

- **8 个混淆/不清点**（§一）：架构概念、术语、视角等容易引起误导的地方
- **9 个架构缺失**（§二）：当前代码或文档与设计不符的地方

来源：`docs/guides/usage-flow-architecture.md` v0.4 §一 8 个混淆点 + §二 9 个缺失（2026-09-04）。

> **源文档演进说明**：源文档 v0.1 → v0.2（拆分出本文件）→ v0.3（聚焦多轮对话 + 实证）→ v0.4（修正 13 处数据流错误）。其中混淆点 4（ChatSession 直连 LLM）所涉及的数据流已在 v0.4 源文档中正确反映为 `call_tool("loop/run")` 路径，标记为已解决。

---

## §一 混淆/不清点（8 个）

### 混淆点 1：L0-L4（OS 视角）vs L1-L5（编排视角）

| 维度 | 内容 |
|------|------|
| **现象** | 新 contributor 读文档时发现两套"5 层"说法，容易混淆 |
| **代码证据** | OS 视角（`docs/specs/architecture.md:65-149`）：L0 Runtime Core / L1 OS Services / L2 Plugin Tools / L3 PDK Contract / L4 Agent App；编排视角（`docs/architecture/agent-orchestration-architecture-2026-08.md:14-49`）：编排层 / 行为编排层 / 认知执行层 / 领域执行层 / 可观测层 |
| **文档证据** | `docs/specs/architecture.md` §2.1 五层抽象；`docs/architecture/agent-orchestration-architecture-2026-08.md` §一 编排全景 |
| **解释** | OS 视角是静态架构分层（L0 核心 → L4 应用），编排视角是运行时执行分层（从编排到可观测）。两者都叫"5 层"但编号体系不同。 |
| **建议理解方式** | 看架构文档时，先确认是 OS 视角还是编排视角；OS 视角：L0 是基础服务，越高层越接近用户；编排视角：从上到下是数据流方向 |

---

### 混淆点 2：MCP 一词多义

| 维度 | 内容 |
|------|------|
| **现象** | "MCP" 在项目中出现时，可能指：1. 外部标准 MCP（Model Context Protocol）；2. LayeredContext 等自研协议；3. ADR-0076 DSL Engine as MCP Server |
| **代码证据** | `docs/adr/adr-0076-dsl-engine-mcp-server.md` — DSL Engine as MCP Server（Proposed）；`docs/specs/architecture.md:51` — MCP 作为 SOTA 对比表格中的外部框架 |
| **文档证据** | ADR-0076 描述："DSL Engine as MCP Server 控制面（D1 stdio+HTTP+SSE + D2 静态 token）"；`capability-application-map-2026-08.md` G9："ADR-0076 DSL Engine as MCP Server → B5 DSL-as-MCP-tool" |
| **解释** | "MCP" 在此项目中通常指 ADR-0076（HydraForge 作为 MCP Server），而非外部标准协议。但文档中可能出现歧义。 |
| **建议理解方式** | 看到"MCP"先判断上下文，是 HydraForge 的 MCP Server 特性还是外部标准；ADR-0076 目前状态是 🔍 Proposed，未 ship |

---

### 混淆点 3：SKILL 隔离执行级别差异

| 维度 | 内容 |
|------|------|
| **现象** | SKILL.md 可以用 posix_spawn / Wasm / in-process 三种方式执行，文档描述不一致 |
| **代码证据** | `include/agenticdsl/skill/skill_interpreter.h:50-90` — SkillInterpreter 定义；`pdk/loop_agent/src/pdk_entry.cpp` — 当前 loop_agent 用 ReactLoop 并非真实 SKILL |
| **文档证据** | `docs/specs/architecture.md:385-443` — Form::Skill 描述；ADR-0066（SkillInterpreter 架构）状态: 🟡 Partial（V1 ship，V2 deferred）|
| **解释** | V1：posix_spawn 进程隔离；V2（deferred）：Wasm 沙箱；in-process：直接调用（无隔离，不推荐） |
| **建议理解方式** | 当前生产代码用 ReactLoop/PlanExecuteLoop/ForkJoinLoop；SkillInterpreter V1 已 ship，但 loop_agent 尚未集成真实 SKILL 执行；隔离级别：posix_spawn > in-process（无隔离）|

---

### 混淆点 4：ChatSession "直连 LLM" 分支已删除但文档未同步

| 维度 | 内容 |
|------|------|
| **状态** | ✅ 已解决（v0.4 源文档正确反映 `call_tool("loop/run")` 路径）|
| **现象** | 旧文档描述 ChatSession 可以直接调用 LLM，但代码中该路径已删除 |
| **代码证据** | `openspec/changes/archive/2026-08-07-loop-agent-bypass/` — fix-loop-agent-bypass change；代码中 loop_agent 通过 `call_tool("loop/run")` 而非直接调用 LLM；`docs/guides/usage-flow-architecture.md` v0.4 §二.2 大图（chat_session.cpp:294 `ChatSession::chat()` + :339 `call_tool("loop/run", loop_args)`）|
| **文档证据** | `examples/pdk_chat_demo/DESIGN.md` — 可能仍描述旧的直连路径（待同步）|
| **解释** | 修复后 loop_agent 统一走 `call_tool("loop/run")`，不再有直连 LLM 的旁路 |
| **建议理解方式** | 以 v0.4 源文档（`docs/guides/usage-flow-architecture.md` §二）为准：实际路径 ChatSession → `call_tool("loop/run")` → loop_agent 插件内部加载 `.agent.md` → DSLEngine 编译执行；没有从 ChatSession 直接到 LLM 的调用。`pdk/loop_agent/src/pdk_entry.cpp:164` 为 loop/run handler 注册点 |

---

### 混淆点 5：LayeredContext L1-L5（5 层）vs OS L0-L4（5 层）编号冲突

| 维度 | 内容 |
|------|------|
| **现象** | LayeredContext 的 L1-L5 和 OS 架构的 L0-L4 都叫"5 层"，但编号差 1 |
| **代码证据** | `include/agenticdsl/types/layered_context.h` — L1 system / L2 user / L3 task / L4 node / L5 raw；`docs/specs/architecture.md:145-149` — L0 Runtime Core / L1 OS Services / L2 Plugin Tools / L3 PDK Contract / L4 Agent App |
| **文档证据** | ADR-0008（LayeredContext）定义 5 层；`docs/specs/architecture.md` 定义 OS 5 层 |
| **解释** | 这是两个独立的 5 层体系，编号差 1：LayeredContext：L1 = 最内层/系统级；OS 架构：L0 = 最内层/核心 |
| **建议理解方式** | LayeredContext 的 L1 相当于 OS 架构的 L0，实际是同一套东西的不同视角描述 |

---

### 混淆点 6：ToolCoordinator vs IToolRegistry 职责边界

| 维度 | 内容 |
|------|------|
| **现象** | 两者都负责工具调用，但职责不清 |
| **代码证据** | `src/common/tools/tool_coordinator.cpp:349` — `ToolCoordinator::execute(meta, ctx, args, token)`；`include/agenticdsl/contract/itool_registry.h` — 9 虚函数接口 |
| **ToolCoordinator 职责** | 1. layer check（检查是否允许某 layer 的工具）；2. ApprovalHandler 调用（审批决策）；3. audit 事件发射（记录调用）；4. 调用 ToolRegistry |
| **IToolRegistry 职责** | 1. 工具注册（register_tool_function）；2. 工具查询（has_tool）；3. 实际调用（call_tool）|
| **解释** | ToolCoordinator 是 Middleware，IToolRegistry 是基础设施。ToolCoordinator 在 IToolRegistry 之上加了一层政策检查。 |
| **建议理解方式** | 需要加 policy/approval/audit → 用 ToolCoordinator；需要基础注册/调用 → 用 IToolRegistry |

---

### 混淆点 7：Plan/Agent/Yolo 三模式 vs 实际审批流

| 维度 | 内容 |
|------|------|
| **现象** | 三种执行模式在代码中如何对应实际审批决策 |
| **代码证据** | `src/common/policy/execution_policy.h` — PlanPolicy / AgentPolicy / YoloPolicy；`src/common/policy/approval_handler.cpp` — process_request 实现 |
| **三模式含义** | Plan：仅 plan 阶段审批；Agent：仅 agent 阶段审批；Yolo：从不审批（⚠️ 危险）|
| **解释** | Yolo 模式跳过了所有审批流程，直接执行工具，生产环境慎用。 |
| **建议理解方式** | 记住 Yolo = 无审批 = 危险；生产环境应用 Plan 或 Agent 模式 |

---

### 混淆点 8：CognitiveWorker / DomainWorkerPool / ForkJoinLoop 三者都是"并行"但语义不同

| 维度 | 内容 |
|------|------|
| **现象** | 三者都涉及并行，但含义不同，容易混淆 |
| **代码证据** | `include/agenticdsl/cognitive/cognitive_worker.h` — CognitiveWorker；`include/agenticdsl/cognitive/domain_worker_pool.h` — DomainWorkerPool；`include/agenticdsl/pdk/agent_loops/fork_join_loop.h` — ForkJoinLoop |
| **三者区别** | CognitiveWorker：多 agent 隔离执行（jthread + stop_token），用于多用户/多会话隔离；DomainWorkerPool：领域任务并行消费（FIFO 队列 + jthread），用于代码生成 + 测试并行；ForkJoinLoop：分支并行执行（fork → parallel → join），用于多工具并行调用 |
| **建议理解方式** | 需要多用户隔离 → CognitiveWorker；需要任务队列并行 → DomainWorkerPool；需要分支并行 → ForkJoinLoop |

---

## §二 架构缺失（9 个）

### G1: compact 破坏性重写

| 维度 | 内容 |
|------|------|
| **缺失内容** | SessionManager 的 compact 方法是 in-place 破坏性重写，不是真正的 append-only 压缩 |
| **影响场景** | A2（多轮对话 + 会话分支）— compact 后历史丢失 |
| **ADR 跟踪** | ADR-0079 v1.2（Pending） |
| **当前状态** | 🔒 Blocked（待架构组决策） |

---

### G2: EventLog query API 自动化校验

| 维度 | 内容 |
|------|------|
| **缺失内容** | EventLog.query() 缺乏自动化测试覆盖 |
| **影响场景** | B3（真实分布式追踪）— query API 行为回归 |
| **ADR 跟踪** | ADR-0080（已 Approved，但 query API 测试覆盖不足） |
| **当前状态** | 🔓 Open |

---

### G3: AgentWorker 完整实现 + spawn_agent + YAML

| 维度 | 内容 |
|------|------|
| **缺失内容** | IAgentRegistry 骨架已 ship，但完整 AgentWorker + YAML 配置 + spawn_agent 未实现 |
| **影响场景** | B2（跨进程多 agent 协作）|
| **ADR 跟踪** | ADR-0082（V1 骨架已 ship，完整实现 T3+T4 未完成）|
| **当前状态** | 🔓 Open（前置 IAgentRegistry ✅ 已 ship）|

---

### G4: Agent↔Agent stream 模式

| 维度 | 内容 |
|------|------|
| **缺失内容** | IAgentComposition 的 stream 模式是占位符，未实现 |
| **影响场景** | B4（Streaming Agent）|
| **ADR 跟踪** | ADR-0060（stream 占位，详见 `agent_composition.h:20`）|
| **当前状态** | 🔓 Open（需 T6）|

---

### G5: Plugin per-agent 隔离

| 维度 | 内容 |
|------|------|
| **缺失内容** | 当前 ToolRegistry 是 per-engine，不是 per-agent，多租户场景下 agent 间无隔离 |
| **影响场景** | B1（Marketplace Agent 部署）|
| **ADR 跟踪** | ADR-0022 + ADR-0082 |
| **当前状态** | 🟡 Partial（per-engine ✅，per-agent ❌）|

---

### G6: Agent hook loop 集成

| 维度 | 内容 |
|------|------|
| **缺失内容** | IAgentHookRegistry 骨架已 ship（ADR-0081），但与 Loop 的集成未完成 |
| **影响场景** | 全部 B/C 类应用的可观测性 |
| **ADR 跟踪** | ADR-0081（V1 骨架已 ship，loop 集成 T6 未完成）|
| **当前状态** | 🔓 Open（前置 IAgentHookRegistry ✅ 已 ship）|

---

### G7: Structured concurrency (scope tree)

| 维度 | 内容 |
|------|------|
| **缺失内容** | C++20 scope tree 未实现，嵌套取消依赖手动管理 |
| **影响场景** | 提升所有 B/C 类应用的可靠性 |
| **ADR 跟踪** | 提案中（无 ADR 编号）|
| **当前状态** | 🟡 Partial（协作取消已 ship，scope tree 未实现）|

---

### G8: OTel 真实 OTLP 客户端

| 维度 | 内容 |
|------|------|
| **缺失内容** | 当前是 NoopSink，真实 OTLP 客户端未实现 |
| **影响场景** | B3（真实分布式追踪）|
| **ADR 跟踪** | ADR-0080（EventLog 已 ship，OTLP 导出待实现）|
| **当前状态** | 🔓 Open（需 T2）|

---

### G9: ADR-0076 DSL Engine as MCP Server

| 维度 | 内容 |
|------|------|
| **缺失内容** | DSL Engine 作为 MCP Server 的完整实现（stdio/HTTP/SSE transport + capability 暴露）|
| **影响场景** | B5（DSL-as-MCP-tool）|
| **ADR 跟踪** | ADR-0076（🔍 Proposed）|
| **当前状态** | 🔓 Open（gated by active-status.md §四）|

---

## §三 跟进机制

### 如何新增一个问题

1. 在本文件 §一（混淆点）或 §二（架构缺失）中新增条目
2. 包含：现象 / 代码证据 / 文档证据 / 解释 / 建议理解方式
3. 关联到具体 ADR 编号（如有）

### ADR 跟踪原则

- **任何架构层问题必须先有 ADR 草案才能 Close**
- 混淆点如果涉及代码与文档不符，需要有对应的修复 commit 或 ADR 决议
- 架构缺失需要追踪到具体的 T1-T22 工程任务

### 与 `capability-application-map-2026-08.md` 的分工

| 文档 | 视角 | 内容 |
|------|------|------|
| `usage-flow-issues.md` | 使用流程视角 | 具体的、诚实的问题发现（8+9 个） |
| `capability-application-map-2026-08.md` | 按层能力视角 | 已 ship 的 31 项能力 + 9 项 open gap |

- **本文件**：从用户/开发者使用流程中发现的实际问题
- **cap-map**：按 L0-L4 分层的已验证能力清单

---

## §四 关联文档

| 文档 | 路径 | 说明 |
|------|------|------|
| 用户视角入口架构 | `docs/guides/usage-flow-architecture.md` | **本文件来源**，主数据流文档（v0.4）|
| 能力应用地图 | `docs/architecture/capability-application-map-2026-08.md` | 31 项 ship + 9 项 gap |
| 五层架构规范 | `docs/specs/architecture.md` | L0-L4 + R1-R5 |
| ADR 状态基线 | `docs/architecture/adr-implementation-status-gap-analysis.md` | ADR 实施状态权威参照 |
| LayeredContext | `include/agenticdsl/types/layered_context.h` | L1-L5 5 层上下文 |
| Session Manager | `src/core/session_manager.cpp` | Session 持久化 |
| ToolCoordinator | `src/common/tools/tool_coordinator.cpp` | 审批 + hook |

---

## §五 变更记录

| 日期 | 版本 | 变更 |
|------|------|------|
| 2026-09-04 | v0.1 | 首次创建：8 个混淆点 + 9 个架构缺失，从 `usage-flow-architecture.md` v0.1 拆分 |
| 2026-09-04 | v0.2 | 同步源文档 v0.4：标记混淆点 4（ChatSession 直连 LLM）✅ 已解决；TL;DR 更新源文档引用与演进说明；关联文档表标注 v0.4 |
