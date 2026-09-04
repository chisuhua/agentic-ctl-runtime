# AgenticDSL 规范 v3.10（当前参考实现版）

> **变更说明**：本文档在 v3.9 基础上更新，反映参考执行器 v1.0 的实际实现状态。主要变更：`llm_call` 重命名为 `dsl_call`（v3.10）；`generate_subgraph` 替代 `llm_generate_dsl` 作为用户可见节点类型；`assign` 字段格式确认为键值映射。

**安全 · 可终止 · 可调试 · 可复用 · 可契约 · 可验证**

## 引言
AgenticDSL 是一套 AI-Native 的声明式动态 DAG 语言，专为单智能体及未来多智能体系统设计。通过构建由DAG驱动AgenticDSL应用，构建被LLM可理解学习的DAG标准库，最终实现一切应用都可以由LLM来驱动和优化, 让LLM成为计算机的主人。

## 一、核心理念与定位

### 1.1 定位

AgenticDSL支持：
- **LLM 可生成**：大模型能输出结构化、可执行的子图
- **引擎可执行**：确定性调度、状态合并、预算控制
- **DAG 可动态生长**：运行时生成新子图，支持思维流与行动流
- **标准库可契约复用**：`/lib/**` 带签名，最小权限沙箱
- **推理可验证进化**：通过 `assert`、Trace、`archive_to` 实现闭环优化

### 1.2 根本范式
| 角色 | 职责 |
|------|------|
| LLM | 程序员：基于真实状态生成可验证子图 |
| 执行器 | 运行时：确定性调度、状态合并、预算控制 |
| 上下文 | 内存：结构可契约、合并可策略、冲突可诊断 |
| DAG | 程序：图可增量演化，支持行动流与思维流 |
| 标准库 | SDK：`/lib/**` 必须带 `signature`，最小权限沙箱 |

### 1.3 设计原则
- **确定性优先**：所有节点必须在有限时间内完成；禁止异步回调；LLM 调用必须声明 `seed` 与 `temperature`；输出需经结构化验证（如 JSON Schema）
- **契约驱动**：接口必须声明，调用必须验证
- **最小权限**：节点/子图需显式声明所需权限；权限组合遵循交集原则
- **可终止性**：全局预算控制，防止无限循环或生成
- **可观测性**：每个节点生成结构化 Trace，支持调试与训练
- **可验证性**：所有推理行为必须可通过 `assert`、Trace 或归档机制进行事后验证

## 二、节点抽象层级（三层架构 + 交互边界）

### 2.1 三层架构
| 层级 | 说明 | 约束 |
|------|------|------|
| 1. 执行原语层（叶子节点） | 规范内置、不可扩展的最小操作单元 | 禁止用户自定义新类型 |
| 2. 标准原语层 | 规范提供的稳定接口实现 | 路径：`/lib/dslgraph/**`, `/lib/memory/**`, `/lib/reasoning/**`, `/lib/conversation/**`，版本稳定 |
| 3. 知识应用层 | 用户/社区扩展的领域逻辑 | 路径：`/lib/workflow/**`, `/lib/knowledge/**` |

✅ 所有复杂逻辑必须通过子图组合实现，禁止在叶子节点中编码高层语义。

注：`/app/**` 不属于上述三层架构，仅为工程组织约定（见附录 A）。

### 2.2 层间契约规则
- **执行 → 标准原语**：仅通过上下文传递数据，禁止直接 API 调用
- **标准原语 → 知识应用**：必须通过 `signature` 暴露能力
- **禁止跨层跳转**：知识应用层不得直接调用执行原语层（必须通过 `/lib/**` 封装）
- **动态子图生成能力**：通过 `/lib/dslgraph/**` 实现，`llm_generate_dsl` 仅用于内部封装
- **强制沙箱隔离**：所有非标准原语层的操作必须通过标准库接口访问外部系统

### 2.3 适配器模式显式化
所有外部系统交互必须通过规范定义的工具接口：
- **工具注册表**：执行器维护 `tool_schema`，声明输入/输出契约
- **适配器隔离**：DAG 仅通过 `tool_call` 与工具交互，不依赖实现细节
- **安全边界**：禁止启动线程、注册回调、直接读写上下文、访问未声明资源

## 三、术语表
| 术语 | 定义 |
|------|------|
| 子图（Subgraph） | 以 `### AgenticDSL '/path'` 开头的逻辑单元 |
| 动态生长 | 通过子图生成在运行时注册新子图至 `/dynamic/**` |
| 契约（Contract） | 由 `signature` 定义的输入/输出接口规范 |
| 软终止 | 子图结束时返回调用者上下文，而非终止整个 DAG |
| 核心标准库 | 强制实现的 `/lib/**` 子图集合（见附录 C） |
| 执行原语层 | 内置叶子节点（如 `assign`, `assert`），不可扩展 |
| 语义能力（Capability） | 执行器可提供的一组原子功能，如 `structured_generate`、`kv_continuation` |

## 四、公共契约

### 4.1 分层上下文模型（LayeredContext）

AgenticDSL v3.10 引入**分层上下文（LayeredContext）**，替代原有的扁平 `Context = nlohmann::json` 模型。分层结构提供清晰的类型边界、压缩感知和工具访问控制。

#### 4.1.1 分层结构（L1-L5）

```
┌─────────────────────────────────────────────────────────────┐
│  LayeredContext (C++ struct)                                │
│                                                             │
│  C++ 类型安全                                                │
│  ├─ 分层边界清晰                                            │
│  ├─ 编译期类型检查                                          │
│  └─ IDE 自动补全                                            │
│                                                             │
│  nlohmann::json 内部                                        │
│  ├─ 保持 AgenticDSL 的 JSON 灵活性                         │
│  ├─ 工具层仍用 JSON 操作                                    │
│  └─ 压缩/序列化透明                                          │
└─────────────────────────────────────────────────────────────┘
```

| 层级 | 名称 | 说明 | 压缩策略 |
|------|------|------|----------|
| L1 | System | Agent 提示词、工具定义、当前任务、DSL 版本 | **永不压缩/丢弃** |
| L2 | Recent | 最近 N 轮对话（完整保留） | 由 ADR-7 ContextCompressor 管理 |
| L3 | Archive | 压缩后的历史对话摘要 | 由 ADR-7 ContextCompressor 管理 |
| L4 | Working | 当前执行状态，工具可读写 | 工具可写入 |
| L5 | Meta | 元数据（schema_version, task_id, total_turns, compress_count） | 只读 |

#### 4.1.2 C++ 结构定义

```cpp
struct LayeredContext {
    // L1: System（永不压缩/丢弃）
    struct SystemLayer {
        std::string agent_prompt;                        // Agent 提示词
        std::vector<ToolDef> tool_definitions;            // 工具定义
        std::string current_task;                         // 当前任务描述
        DSLVersion dsl_version;                           // DSL 版本
    } system;

    // L2: Recent（最近 N 轮，完整保留）
    std::vector<ContextTurn> recent_turns;               // 最近 5 轮

    // L3: Archive（压缩后历史）
    std::vector<ArchiveEntry> archive;                   // 压缩归档

    // L4: Working（当前执行状态，工具可写）
    struct WorkingLayer {
        nlohmann::json data;                             // 工具通过 state.write 写入
        std::string& operator[](const std::string& key) { return data[key]; }
        const nlohmann::json& at(const std::string& key) const { return data.at(key); }
        bool contains(const std::string& key) const { return data.contains(key); }
    } working;

    // L5: Meta（元数据）
    struct MetaLayer {
        int schema_version = CURRENT_SCHEMA_VERSION;     // Schema 版本
        std::string task_id;                             // 任务 ID
        int total_turns = 0;                             // 累计轮次
        int compress_count = 0;                           // 累计压缩次数
        std::chrono::steady_clock::time_point created_at;
        std::chrono::steady_clock::time_point last_updated_at;
    } meta;
};

struct ArchiveEntry {
    int turn_start;                                      // 起始轮次
    int turn_end;                                        // 结束轮次
    std::string conversation_summary;                    // 对话摘要
    std::vector<ToolResultSummary> tool_summaries;       // 工具摘要
    std::chrono::steady_clock::time_point timestamp;
};

struct ContextTurn {
    int turn_number;
    std::string user_input;
    std::string llm_output;
    std::vector<ToolCall> tool_calls;                    // 该轮工具调用
    std::string error;                                   // 错误（如果有）
};
```

#### 4.1.3 JSON 表示

```json
{
    "system": {
        "agent_prompt": "You are a helpful assistant...",
        "tool_definitions": [
            {"name": "fs.read", "description": "Read file", "params": {...}},
            {"name": "state.write", "description": "Write to working memory", "params": {...}}
        ],
        "current_task": "Analyze the codebase and generate a report",
        "dsl_version": "3.10"
    },

    "recent_turns": [
        {
            "turn_number": 11,
            "user_input": "Show me the main files",
            "llm_output": "I'll analyze the structure...",
            "tool_calls": [
                {"name": "fs.read", "arguments": {"path": "src/main.cpp"}, "result": "..."}
            ]
        },
        {
            "turn_number": 12,
            "user_input": "Good, now summarize",
            "llm_output": "Based on my analysis...",
            "tool_calls": []
        }
    ],

    "archive": [
        {
            "turn_start": 1,
            "turn_end": 5,
            "conversation_summary": "User explored project structure, read README and main files. Identified 3 key modules.",
            "tool_summaries": [
                {"tool": "fs.read", "key_output": "README.md: 200 lines", "success": true},
                {"tool": "fs.read", "key_output": "src/main.cpp: 150 lines", "success": true}
            ],
            "timestamp": "2026-05-12T10:30:00Z"
        }
    ],

    "working": {
        "data": {
            "analysis_results": {"modules_found": 3, "key_files": ["a.cpp", "b.cpp"]},
            "user_preferences": {"verbose": false}
        }
    },

    "meta": {
        "schema_version": 1,
        "task_id": "task_001",
        "total_turns": 12,
        "compress_count": 1,
        "created_at": "2026-05-12T10:00:00Z",
        "last_updated_at": "2026-05-12T10:35:00Z"
    }
}
```

#### 4.1.4 工具访问：state.read / state.write

LayeredContext 通过 `StateTools` 类提供唯一的工具访问入口：

```cpp
class StateTools {
public:
    explicit StateTools(LayeredContext& ctx) : ctx_(ctx) {}

    // 读接口
    nlohmann::json read(const std::string& path) {
        auto [layer, key] = parse_path(path);
        switch (layer) {
            case Layer::System:
                return ctx_.system.at(key);              // system.* 只读
            case Layer::Recent:
                return ctx_.recent_turns.at(std::stoi(key));  // recent.* 只读
            case Layer::Archive:
                return ctx_.archive.at(std::stoi(key));  // archive.* 只读
            case Layer::Working:
                return ctx_.working.at(key);             // working.data.* 可读写
            case Layer::Meta:
                return ctx_.meta.at(key);               // meta.* 只读
            default:
                throw StateError{"Invalid path: " + path};
        }
    }

    // 写接口（只允许 working.data.*）
    void write(const std::string& path, const nlohmann::json& value) {
        auto [layer, key] = parse_path(path);
        if (layer != Layer::Working) {
            throw StateError{
                "Write not allowed to " + path + ". Only working.* is writable."
            };
        }
        ctx_.working[key] = value;
    }

    std::vector<std::string> list_keys(const std::string& prefix);

private:
    LayeredContext& ctx_;
    enum class Layer { System, Recent, Archive, Working, Meta };
    std::pair<Layer, std::string> parse_path(const std::string& path);
};
```

**DSL 中使用 state 工具**：

```yaml
AgenticDSL `/main/analyze`
type: tool_call
tool: state.write
arguments:
  path: "working.data.analysis_results"
  value: {"modules": 3, "status": "in_progress"}
next: "/main/continue"

AgenticDSL `/main/use_memory`
type: tool_call
tool: state.read
arguments:
  path: "working.data.previous_results"
next: "/main/continue"
```

#### 4.1.5 路径前缀与访问权限

| 路径前缀 | 读权限 | 写权限 | 说明 |
|----------|--------|--------|------|
| `system.*` | ✅ | ❌ | Agent 配置，工具禁止修改 |
| `recent.*` | ✅ | ❌ | 由引擎管理 |
| `archive.*` | ✅ | ❌ | 由 ADR-7 压缩器管理 |
| `working.*` | ✅ | ✅ | 工具的"沙箱"写入区域 |
| `meta.*` | ✅ | ❌（仅引擎） | 元数据，只读 |

#### 4.1.6 合并策略与分层集成

LayeredContext 的各层遵循以下合并策略：

| 策略 | 行为说明 |
|------|----------|
| `error_on_conflict`（默认） | 任一字段在多个分支中被写入 → 报错终止 |
| `last_write_wins` | 以最后完成的节点写入值为准 |
| `deep_merge` | 递归合并对象；数组完全替换；标量覆盖 |

**分层感知的压缩**：
- L2（Recent）和 L3（Archive）由 ADR-7 ContextCompressor 管理
- 压缩后的摘要存储在 `archive[]`，原始对话移至 `recent_turns[]`
- System 层永不压缩，确保 Agent 行为一致性

### 4.2 Inja 模板引擎（安全模式）
✅ **允许**：变量（如 `{{ $.path }}`）、条件、循环、表达式  
❌ **禁止**：`include`/`extends`、环境变量、任意代码执行  
🔁 **性能优化**：缓存相同模板+上下文的渲染结果  

**时间上下文**：可通过 `$.now` 访问（ISO 8601 字符串），非模板函数，由执行器注入。

### 4.3 节点通用字段
| 字段 | 说明 |
|------|------|
| `type` | 节点类型（必需） |
| `next` | 路径或路径列表（支持 `@v1`） |
| `permissions` | 权限声明（见 7.2） |
| `context_merge_policy` | 字段级合并策略 |
| `on_success` | 成功后动作（如 `archive_to(...)`） |
| `on_error` | 错误跳转路径（若未定义，则终止当前子图） |
| `expected_output` | 期望输出（用于验证/训练） |
| `curriculum_level` | 课程难度标签（如 `beginner`） |

❌ **移除 `dev_comment`**：使用标准 Markdown 注释（如 `<!-- debug: ... -->`）

**说明**：`expected_output` 用于单次执行验证，而 `signature.outputs` 用于子图接口契约。前者记录具体期望值用于 Trace 验证，后者定义调用契约。

## 五、核心叶子节点定义（执行原语层）

### 5.1 `assign`
**语义**：安全赋值到上下文（Inja 模板渲染）

`assign` 字段为**键值对映射**，键为目标上下文字段名，值为 Inja 模板表达式：
```yaml
type: assign
assign:
  preferred_drink: "coffee"                    # 直接值
  welcome_msg: "Hello, {{ user.name }}!"       # Inja 模板
  item_count: "{{ length(items) }}"            # 表达式
next: "/main/next_step"
```

多字段并发赋值示例：
```yaml
type: assign
assign:
  num1: "15"
  num2: "27"
next: "/main/compute"
```

**字段表**：
| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| assign | object | ✅ | 键值对映射，键为上下文字段名，值为 Inja 模板字符串 |
| next | string/list | ❌ | 后继节点路径 |

> ⚠️ **注意**：`assign` 中的每个键直接对应上下文顶层字段（或通过 `parent.child` 嵌套路径表示）。`assign.expr` / `assign.path` 仅是普通键名，不具有特殊含义。

**执行器行为**：
- 按 Inja 模板渲染后写入上下文，失败则抛出异常
- 多个键并发写入，若有合并冲突则遵循当前合并策略
- ⏳ **计划中**（v1.x）：`meta.ttl_seconds` / `meta.persistence` 字段（上下文字段 TTL）当前参考实现不支持

### 5.2 `tool_call`
**语义**：调用注册工具（带权限检查）  
**关键字段**：`tool`, `arguments`, `output_mapping`  
**权限要求**：必须声明 `permissions`（如 `tool: web_search`）

### 5.3 `codelet_call`
**语义**：执行沙箱代码（带安全策略）  
**关键字段**：`runtime`, `code`, `security`  
**权限要求**：必须声明 `permissions`（如 `runtime: python3`）

> ⚠️ **实现状态**：`codelet_call` 节点类型在参考执行器 v1.0 中**未实现**，计划在 v1.x 迭代中引入。当前引擎遇到此类型节点会忽略（向前兼容模式）。如需沙箱代码执行能力，请通过 `tool_call` 调用已注册的工具实现。

### 5.4 `assert`
**语义**：验证条件，失败则跳转  
**关键字段**：`condition`（Inja 布尔表达式）, `on_failure`
```yaml
type: assert
condition: "{{ len($.roots) == 1 }}"
on_failure: "/self/repair"
```

### 5.5 `fork` / `join`
**语义**：显式并行控制  
- `fork.branches`: 路径列表
- `join.wait_for`: 依赖列表, `merge_strategy`

**依赖解析时机**：执行器必须在节点入调度队列前解析 `wait_for` 表达式  
**禁止**：在执行中动态变更依赖拓扑

### 5.6 `end`
**语义**：终止当前子图  
**关键字段**：
- `termination_mode`: `hard`（默认）或 `soft`
- `output_keys`: 仅合并指定字段到父上下文（`soft` 模式）

### 5.7 `generate_subgraph`（规范用名：`llm_generate_dsl`）
**语义**：委托 LLM 生成结构化子图并动态注入调度器  
**YAML 节点类型字符串**：`generate_subgraph`（参考实现使用此名）  
**输出**：LLM 必须生成 `### AgenticDSL '/dynamic/...'` 块  
**权限**：`generate_subgraph: { max_depth: N }`  
**namespace_prefix** 强制为 `/dynamic/`，禁止 `/lib/` 或 `/main/`

```yaml
type: generate_subgraph
prompt_template: "{{ $.expr }} 求解失败。请重写为标准形式并生成新 DAG。"
output_keys: ["generated_graph_path"]
signature_validation: warn      # strict | warn | ignore
on_signature_violation: "/self/fallback"
next: "/dynamic/repair_123"
```

**字段表**：
| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| prompt_template | string | ✅ | Inja 模板提示词（执行前渲染） |
| output_keys | string/list | ✅ | 写入上下文的输出字段名 |
| signature_validation | string | ❌ | `strict`（默认）/ `warn` / `ignore` |
| on_signature_violation | string | ❌ | 签名校验失败跳转路径 |
| next | string/list | ❌ | 成功后跳转路径 |

**执行器行为**：
- 渲染 `prompt_template` 后通过 LLM 工具生成 DSL 文本
- 解析生成的 `### AgenticDSL '/dynamic/...'` 块，动态注入调度器
- 生成的子图通过 `AppendGraphsCallback` 回调注册，后续节点可通过 `next` 或 `wait_for` 引用

**Trace 输出**：
```json
{
  "generate_subgraph": {
    "generated_paths": ["/dynamic/plan_1", "/dynamic/plan_2"],
    "signature_validation": "warn"
  }
}
```

> 📌 规范术语 `llm_generate_dsl` 是内部实现名，用户在 DSL 文件中应始终使用 `generate_subgraph` 作为节点类型字符串。

### 5.8 `start`
无操作，跳转到 `next`

### 5.9 `dsl_call`（v3.10 新名；旧名 `llm_call` 保留为向后兼容别名）

> **v3.10 变更**：`llm_call` 重命名为 `dsl_call`，以更准确地描述其语义——通过已注册的 LLM 工具生成文本/DSL 内容。旧式 `llm_call` 类型字符串在参考执行器中继续有效（创建 `DSLNode`，使用默认工具名 `"llama-default"`）。

**语义**：通过 `ToolRegistry` 中注册的 LLM 工具生成文本，写入上下文字段  
**节点类型字符串**：`dsl_call`（推荐）或 `llm_call`（向后兼容）

**完整格式**（`dsl_call`）：
```yaml
type: dsl_call
prompt_template: "请分析 {{ $.input }} 并给出结论"   # Inja 模板，执行前渲染
llm_tool_name: "llama-7b"                             # C1 后保留为可选字段；首选 set_llm_provider() 注入 ILLMProvider
llm_params:
  temperature: 0.7        # 默认 0.7
  max_tokens: 2048        # 默认 2048 (Track 0.1 M1.3 调整)
  top_p: 0.95             # 默认 0.95
  n_ctx: 2048             # 默认 2048
  n_threads: 4            # 默认 4
  model: "llama-2-7b"    # 可选，覆盖工具默认模型
output_keys: ["analysis"]  # 写入上下文的字段名（LLM 输出文本写入第一个 key）
next: "/main/verify"
```

> **C1（2026-06-08）变更说明**：参考执行器 v1.0 已迁移到 `ILLMProvider` 流式接口
> （ADR-0001）。`dsl_call` 节点的 LLM 提供方由 `DSLEngine::set_llm_provider()` 注入，
> 不再通过 `register_llm_tool()` 显式注册（后者保留为向后兼容路径）。
> 默认 provider 为 `MockLLMProvider`（CI 无需本地 LLM）。

**向后兼容格式**（`llm_call`，使用默认工具 `"llama-default"`）：
```yaml
type: llm_call
prompt_template: "Summarize: {{ input }}"
output_keys: "summary"
next: "/main/end"
```

**字段表**：
| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| prompt_template | string | ✅ | Inja 模板提示词，执行前渲染 |
| llm_tool_name | string | ✅（dsl_call）| 已注册的 LLM 工具名 |
| llm_params | object | ❌ | 生成参数（temperature, max_tokens, top_p, n_ctx, n_threads, model） |
| output_keys | string/list | ✅ | LLM 输出文本写入的上下文字段名 |
| next | string/list | ❌ | 后继节点路径 |

**执行器行为**：
- 渲染 `prompt_template` 后，通过 `ToolRegistry::call_llm_tool(llm_tool_name, prompt, params)` 生成文本
- 生成结果（`LLMResult.text`）写入 `output_keys[0]` 对应的上下文字段
- 若 `llm_tool_name` 未注册，抛出运行时错误
- `output_keys` 为空时抛出运行时错误

**Trace 输出**：
```json
{
  "dsl_call": {
    "llm_tool_name": "llama-7b",
    "prompt_tokens": 120,
    "completion_tokens": 80
  }
}
```

**权限要求**：建议声明对应推理权限（如 `reasoning: llm_generate`）

**注册方式（C++ 宿主代码，C1 后推荐）**：
```cpp
auto engine = DSLEngine::from_markdown(dsl_content);  // 默认创建 MockLLMProvider
// 真实 LLM 场景（C1 路径）：
engine->set_llm_provider(std::make_unique<LlamaAdapterProvider>(
    LlamaAdapter::Config{ .model_path = "/models/llama-2-7b.gguf" }));
// 或者云端：
// engine->set_llm_provider(std::make_unique<CloudLLMAdapter>(config));
```

> **向后兼容**：旧式 `engine->register_llm_tool(name, tool)` 路径仍可用，
> 但建议迁移到 `set_llm_provider` 以获得完整的 `ILLMProvider` 能力
> （流式生成、错误传递契约 `IGenerationStream::error()`、取消支持等）。

## 五点五、流式 LLM 接口（ADR-1 类型扩展）

> **v3.10 新增**：本节定义 ADR-1 流式接口的类型和契约，适用于 `dsl_call` 的流式模式。

### 5.5.1 IGenerationStream 接口

拉取式流式生成器接口，调用方通过 `next()` 主动拉取 token：

```cpp
class IGenerationStream {
public:
    virtual ~IGenerationStream() = default;

    // 阻塞直到下一个 token 可用
    // - 返回 std::string: 有效的 token
    // - 返回 std::nullopt: 生成结束
    // - 若 token.stop_requested() 为 true，应立即返回 std::nullopt
    virtual std::optional<std::string> next(std::stop_token token) = 0;

    // 检查生成是否仍在进行
    virtual bool is_active() const = 0;
};
```

**设计原则**：
- 拉取模式（调用方控制节奏）而非推送模式（回调）
- `std::stop_token` 是 C++20 标准协作取消机制
- 即使后端不支持真正的请求取消（如 Anthropic SSE），也可在 `next()` 中检测停止请求后丢弃后续数据

### 5.5.2 LLMError 结构

结构化错误类型，替代传统的 `bool success + string error` 反模式：

```cpp
struct LLMError {
    enum class Code {
        NetworkError,       // 网络中断，可重试
        RateLimited,        // 限流，带 retry_after
        AuthenticationError, // 认证失败，不重试
        Cancelled,          // 用户取消
        InvalidRequest,     // 参数错误
        ServerError,        // 服务端错误，可重试
        ContextOverflow,    // 上下文超限
        Unknown
    };

    Code code;
    std::string message;
    std::optional<std::chrono::seconds> retry_after;

    bool retryable() const {
        return code == Code::RateLimited
            || code == Code::ServerError
            || code == Code::NetworkError;
    }
};
```

**错误码说明**：
| 错误码 | 可重试 | 说明 |
|--------|--------|------|
| `NetworkError` | ✅ | 网络中断，客户端可自行重试 |
| `RateLimited` | ✅ | 限流，`retry_after` 指明等待时间 |
| `AuthenticationError` | ❌ | 认证失败，不应重试 |
| `Cancelled` | ❌ | 用户主动取消 |
| `InvalidRequest` | ❌ | 参数错误，修复前无法重试 |
| `ServerError` | ✅ | 服务端错误，可重试 |
| `ContextOverflow` | ⚠️ | 上下文超限，可能需要压缩策略 |
| `Unknown` | ❌ | 未知错误 |

### 5.5.3 generate_stream() 方法

ILLMProvider 的流式生成方法：

```cpp
class ILLMProvider {
public:
    virtual ~ILLMProvider() = default;

    // 同步模式（简单场景用）
    virtual std::expected<GenerationResult, LLMError>
        generate(const GenerationRequest& req,
                 std::stop_token token = {}) = 0;

    // 流式模式（返回 stream handle，调用方拉取）
    virtual std::unique_ptr<IGenerationStream>
        generate_stream(const GenerationRequest& req,
                        std::stop_token token = {}) = 0;

    virtual bool is_available() const = 0;
    virtual std::string name() const = 0;
};
```

**使用示例**：
```cpp
std::stop_source stop_src;
auto stream = provider.generate_stream(req, stop_src.get_token());

while (auto token = stream->next(stop_src.get_token())) {
    std::cout << *token << std::flush;
    if (*token == "<DONE>") break;
}

// 取消执行
stop_src.request_stop();
```

### 5.5.4 dsl_call 流式模式

`dsl_call` 支持流式输出，通过 `stream: true` 字段启用：

```yaml
type: dsl_call
prompt_template: "请生成一个故事"
llm_tool_name: "llama-7b"
llm_params:
  temperature: 0.8
  max_tokens: 1024
output_keys: ["story"]
stream: true              # 启用流式输出
next: "/main/process"
```

**执行器行为**：
- 调用 `ILLMProvider::generate_stream()` 而非 `generate()`
- 每个 token 实时写入 `output_keys[0]` 对应的上下文字段
- 支持 `std::stop_token` 取消
- 流式模式下 Trace 输出包含 `streamed_tokens` 计数

**取消机制**：
- Ctrl+C 或超时触发 `stop_source.request_stop()`
- `IGenerationStream::next()` 检测到 `token.stop_requested()` 时立即返回 `std::nullopt`
- 执行器清理资源并跳转到 `on_error`（若定义）

## 六、统一文档结构

### 6.1 路径命名空间（关键强化）
| 命名空间 | 用途 | 可写入？ | 可复用？ | 签名要求 |
|----------|------|----------|----------|----------|
| `/lib/**` | 标准库（只读） 包含推理、记忆、图操作等契约化组件 | ❌ 禁止运行时写入或覆盖 | ✅ 全局可用 | ✅ 强制 |
| `/dynamic/**` | 运行时生成子图 | ✅ 自动写入 | ⚠️ 会话内有效 | ⚠️ 可选（验证后） |
| `/main/**` | 主流程入口 | ✅ 允许 | ❌ | ❌ |
| `/app/**` | 工程别名，语义等价于 `/main/**` | ✅ 允许 | ❌ | ❌ |
| `/__meta__` | 元信息（版本、入口、资源声明） | ✅（仅解析阶段） | N/A | N/A |

**执行器行为**：
- 违反命名空间写规则（如尝试写入 `/lib/**`）→ `ERR_NAMESPACE_VIOLATION`
- 执行器应默认支持 `/app/**` 作为 `/main/**` 的语义等价命名空间。在沙箱或高安全环境中，可通过配置显式禁用，此时归档或引用 /app/** 路径应返回 ERR_NAMESPACE_DISABLED

### 6.2 子图签名（Subgraph Signature）
所有 `/lib/**` 必须声明：
```yaml
signature:
  inputs:
    - name: expr
      type: string
      required: true
  outputs:
    - name: roots
      type: array
      schema: { type: array, items: { type: number }, minItems: 1 }
  version: "1.0"
  stability: stable  # stable / experimental / deprecated
```

### 6.3 显式执行入口
```yaml
AgenticDSL `/__meta__`
version: "3.10"
mode: dev
entry_point: "/main/start"  # ✅ 必需：DAG 执行入口路径
execution_budget:
  max_nodes: 20
  max_subgraph_depth: 2
```

**规则**：
- 唯一性：每个 `.agent.md` 仅允许一个 `entry_point`
- 必需字段；若缺失 → `ERR_MISSING_ENTRY_POINT`
- 必须指向文档中已定义的子图
- 推荐将入口设为 `/main/start`（类型为 `start` 或 `assign`）

### 6.4 资源声明（Resource Declaration）
```yaml
AgenticDSL `/__meta__/resources`
type: resource_declare
resources:
  - type: tool
    name: web_search
    scope: read_only
  - type: runtime
    name: python3
    allow_imports: [json, sympy]
  - type: network
    outbound:
      domains: ["api.mathsolver.com"]
  - type: memory
    backends: [kg, vector]
  - type: knowledge_graph
    capabilities:
      - multi_hop_query
      - evidence_path_extraction
      - subgraph_write
  - type: generate_subgraph
    max_depth: 2
  - type: tool
    name: image_generator
    scope: write
    capabilities: [text_to_image, high_res]
    rate_limit: "5/min"
  - type: reasoning
    capabilities:
      - text_generation
      - structured_generate
      - kv_continuation
      - stream_output
      - speculative_decode
  - type: tool
    name: native_inference_core
    scope: internal
    capabilities: [tokenize, kv_alloc, model_step, compile_grammar, stream_until]
```

**路径固定**：必须为 `/__meta__/resources`  
**非执行性**：不参与 DAG 执行流，不计 `max_nodes`，无 `next` 字段  
**启动时验证**：执行器在 DAG 启动前一次性验证所有声明资源  
**验证失败**：立即终止，返回错误码 `ERR_RESOURCE_UNAVAILABLE`  
**与权限联动**：声明的资源自动成为后续节点权限检查的上下文依据  

**资源类型定义**：
| 类型 | 字段 | 示例 |
|------|------|------|
| tool | name, scope, capabilities, rate_limit | image_generator, ["text_to_image"], "5/min", web_search, read_only |
| runtime | name, allow_imports | python3, [json, re] |
| network | outbound.domains | ["api.example.com"] |
| memory | backends | [kg, vector, profile] |
| knowledge_graph | capabilities | [multi_hop_query, evidence_path_extraction] |
| generate_subgraph | max_depth | 2 |
| reasoning | capabilities | [text_generation, structured_generate, kv_continuation] |
| native_inference_core | capabilities | [tokenize, kv_alloc, model_step] |

**语义规则**：
- **能力声明**：声明所需能力（如 `evidence_path_extraction`），而非具体实现
- **非强制绑定**：`backend_hint` 仅作为优化提示，执行器可选择任意满足能力的后端
- **权限映射**：`reasoning` 能力声明必须与 `llm_call` 字段支持明确对应：
  - `structured_generate` → `output_schema`
  - `kv_continuation` → `kv_handle`
  - `stream_output` → `stop_condition`
  - `speculative_decode` → `draft_model`, `max_speculative_tokens`
- **降级机制**：若未声明所需能力，执行器应尝试使用基础三元组查询（`query_latest`），若完全不支持，返回 `ERR_UNSUPPORTED_CAPABILITY`

### 6.6 `exec:` declarative style（ADR-0072 D3 / Phase 6c C6）

> **状态**：✅ **已实现**（2026-09-02，`from-roadmap-phase-6c-execution-dsl` C6 branch，Evidence Gate Conditional）。

`exec: [...]` 是**并行子节点组的声明式语法糖**：等价于手写 `fork` + N 个并行子节点 + `join` 结构，供 LLM 以更紧凑形式表达并行工具调用。

#### 语法

`exec:` key 可出现在子图 `nodes` 列表的节点定义中，值为数组。每个数组元素可以是：

1. **字符串简写**：工具名（如 `"shell/exec"`）→ 自动展开为 `tool_call` 节点
2. **对象定义**：完整节点字段（`type` + 该类型的必填字段）→ 复用 NodeFactory 创建对应节点

```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: fan_out
    exec: [shell/exec, fs/read, custom/tool]   # 字符串简写: 3 个并行 tool_call
    next: [/main/after]
  # 等价于手写:
  # - id: fan_out_fork
  #   type: fork
  #   fork:
  #     branches: [/main/fan_out/branch_0, /main/fan_out/branch_1, /main/fan_out/branch_2]
  #   next: [/main/fan_out_join]
  # - id: fan_out/branch_0
  #   type: tool_call
  #   tool: shell/exec
  #   ...
  # - id: fan_out/branch_1
  #   type: tool_call
  #   tool: fs/read
  #   ...
  # - id: fan_out/branch_2
  #   type: tool_call
  #   tool: custom/tool
  #   ...
  # - id: fan_out_join
  #   type: join
  #   join:
  #     wait_for: [/main/fan_out/branch_0, /main/fan_out/branch_1, /main/fan_out/branch_2]
  #   next: [/main/after]
# --- END AgenticDSL ---
```

#### fork/join 展开规则

`exec: [child_0, child_1, ..., child_N]`（N ≥ 2）在解析时展开为：

1. **fork 节点**：`<base_path>_fork`，`branches` = 所有子节点路径，`next` = join 路径
2. **N 个子节点**：`<base_path>/branch_<i>`，各自 `next` = join 路径
3. **join 节点**：`<base_path>_join`，`wait_for` = 所有子节点路径，`next` = 原节点的 `next` 字段

生成的 DAG 边集合与手写 fork/join **逐边等价**（单元测试断言 `extract_edges(exec_graph) == extract_edges(manual_graph)`）。

#### 嵌套限制

- **`max_exec_depth = 1`**：仅支持一层展开，`exec: [exec: [...]]` 嵌套 → 抛 `ParseError`（`'exec' nesting exceeds max_exec_depth=1`）
- 递归展开留 Sprint 28+（design.md Open Question 1）

#### 单元素优化

`exec: [single_tool]` → **不生成 fork/join**，直接创建单个 `tool_call` 节点执行，零 DAG 开销（spec "exec with single item behaves like no-op fork/join"）。

### 6.7 双语法共存期（ADR-0072 D5 / Phase 6c C7）

> **状态**：✅ **已实现**（2026-09-02，`from-roadmap-phase-6c-execution-dsl` C7 branch）。

D2/D3 ship 后进入**双语法共存期**：legacy 语法（`-> output_name` 边引用、手写 `type: fork`/`type: join`）与新版语法（`exec:`）可并存，**不强制迁移、不废弃旧语法**。

#### 迁移原则（ADR-0072 §不变量 3）

- 新旧语法 100% 向后兼容，现有 `.agent.md` 无修改通过解析
- 新语法使用率 ≥ 50% 才评估废弃时机；**超前废弃视为违规**
- 共存期 C7 lint 仅发 **warning（exit 0）**，不阻断 commit

#### `# lint:disable dual-syntax` 注释规范

用户可在 legacy 语法行**前一行**添加注释以豁免该行警告：

```yaml
- id: b
  type: assign
  assign:
    # lint:disable dual-syntax
    x: "-> output_name"   # 本行 legacy 引用不产生 lint 警告
```

规则：
- 注释格式：行首 `# lint:disable dual-syntax`（`#` 后可跟任意空白）
- **作用域**：抑制**下一行**的警告（spec "WHEN line 41 contains `# lint:disable dual-syntax` THEN no warning for line 42"）
- 注释行自身不触发 lint 警告

### 6.8 C7 lint 工具使用说明（`dual_syntax_lint`）

> **状态**：✅ **已实现**（2026-09-02）。

`dual_syntax_lint` 是独立可执行 lint 工具，检测 `.agent.md` 中的 legacy 语法并给出修复建议。

#### CLI

```bash
dual_syntax_lint [options] <file...>

Options:
  --include-historical    Lint 所有文件（默认跳过历史文件）
  --ship-timestamp <date> 覆盖 D2/D3 ship 时间戳（YYYY-MM-DD，默认 2026-09-02）
  --help, -h              显示帮助
```

#### 输出格式

```
<file>:<line>: warning: legacy syntax '<match>'; consider '<suggestion>'
```

示例：
```
new_file.agent.md:11: warning: legacy syntax '-> output_name'; consider '$output_name'
new_file.agent.md:8: warning: legacy syntax 'type: fork'; consider 'exec: [...]'
```

#### 检测规则

| 模式 | 匹配 | 建议 |
|------|------|------|
| 边引用 | `-> identifier` / `→ identifier` | `$identifier` |
| 手写并行 | `type: fork` / `type: join` | `exec: [...]` |

#### 新文件 heuristic（D-4）

默认仅检测**新提交**的 `.agent.md` 文件：
- `is_new_file(path)` = 文件 mtime ≥ ship 时间戳 **且** git log 显示最近提交（或无 git 历史视为新文件）
- 历史 shipped 文件（mtime 早于 ship 时间戳）**不重报**
- `--include-historical` 手动覆盖（默认 off）

#### CI 集成建议

- lint 仅发 warning（exit 0），可在 CI 中作为**非阻塞**提示步骤运行
- 可选集成 pre-commit hook（design.md Risks §双语法共存期 建议，非强制）



> **状态**：🔮 Planned — 本节为前瞻示例，DSL 解析层由独立 W5 提案交付（Phase 6c 收官前）。Backend 抽象与 `EnvValidationHook` 已 ship（Wave 3-A `from-roadmap-phase-6c-execution-envbackend`）。

`shell.exec` 节点（来自 [ADR-0071 §决策 D6](./adr/adr-0071-llm-native-agenticdsl-architecture.md)）支持可选 `backend:` 字段指定执行环境，由 `IEnvBackend` 抽象统一接口（详见 [docs/specs/env-backend.md](./env-backend.md)）。

#### 示例 — `local` backend

```yaml
- type: shell_exec
  name: list_tmp
  cmd: /bin/ls
  args: ["-la", "/tmp"]
  backend: local
  working_dir: /tmp
  env:
    LANG: en_US.UTF-8
  __approved: true              # local backend 默认 requires_approval=true
```

#### 示例 — `docker:<image>:<tag>` ephemeral

```yaml
- type: shell_exec
  name: run_pytest
  cmd: pytest
  args: ["-v", "tests/"]
  backend: docker:python:3.12@sha256:abc123def456...
  env:
    PYTHONPATH: /app
  # ephemeral docker 默认 requires_approval=false, 无需 __approved
```

#### 示例 — `docker:prod` named container

```yaml
- type: shell_exec
  name: prod_migration
  cmd: /opt/migrate.sh
  args: ["--dry-run"]
  backend: docker:prod
  __approved: true              # docker:prod 默认 requires_approval=true
```

#### `backend:` 字段语义

| 取值 | 解析为 | 默认审批 |
|------|--------|----------|
| `local` | `LocalBackend` (fork+execve) | ✅ 需要 |
| `docker:<container_id>` | `DockerBackend` mode (a) exec into existing | 视 BackendPolicy |
| `docker:<image>:<tag>[@sha256:digest]` | `DockerBackend` mode (b) ephemeral | ❌ 不需要（ephemeral） |
| `docker:prod` | `DockerBackend` mode (a) prod 命名容器 | ✅ 需要 |
| 未指定 / 空 | `local`（默认 backend） | ✅ 需要 |

#### 安全检查（强制经过 EnvValidationHook pre-hook）

无论 `backend:` 取何值，所有 `shell.exec` 节点必经 [EnvValidationHook](./env-backend.md#六envvalidationhook-c13) 四步 policy 校验：

1. backend spec 命中 `BackendPolicy::find_policy()`（未知 → `Deny: "unknown backend"`）
2. docker 镜像在 `image_allowlist`（非空时强制）
3. `env.<NAME>` 键在 `allowed_env_vars` 白名单（含 `"*"` 通配）
4. `working_dir` 前缀匹配 `allowed_paths`（含 `"*"` 通配）
5. `requires_approval=true` 需 `__approved: true` 标记

详见 [docs/security/backend-policy.md §四 决策流程图](../security/backend-policy.md)。

## 七、安全与工程保障

### 7.1 标准库契约强制
- 启动时预加载并校验所有 `/lib/**`
- LLM 生成时 `available_subgraphs` 必须含 `signature`
- 任何尝试写入 `/lib/**` 的行为立即终止（`ERR_NAMESPACE_VIOLATION`）

### 7.2 权限与沙箱
**权限格式**为结构化对象：
```yaml
permissions:
  - tool: web_search → scope: read_only
  - runtime: python3 → allow_imports: [json, re]
  - network: outbound → domains: ["api.example.com"]
  - generate_subgraph: { max_depth: 2 }
```

**权限组合规则**：
- **交集原则**：节点权限 ∩ 父上下文授权权限
- **拒绝优先**：任一缺失 → 跳转 `on_error`
- **权限降级**：子图调用时权限只能减少
- **资源声明是权限的前置契约**：执行器在启动时验证 `/__meta__/resources` 中声明的资源可用性后，才允许执行声明了对应 `permissions` 的节点

**推理权限类型**：
| 权限 | 说明 | 最小权限范围 |
|------|------|------------|
| reasoning: llm_generate | 基础文本生成 | 仅限 `llm_call` 调用 |
| reasoning: structured_generate | 结构化输出（需 `output_schema`） | 同上 |
| reasoning: stream_output | 流式终止（需 `stop_condition`） | 同上 |
| reasoning: speculative_decode | 推测解码 | 同上 |

### 7.3 可观测性（Trace Schema）
兼容 OpenTelemetry，记录：执行状态、上下文变更、输出匹配、LLM 意图、预算快照

**通用 Trace 结构**：
```json
{
  "node_id": "node-123",
  "node_type": "llm_call",
  "timestamp": "2025-11-10T08:30:00Z",
  "status": "success",
  "latency_ms": 450,
  "context_snapshot": { /* 变更前后对比 */ },
  "budget_snapshot": {
    "nodes_left": 15,
    "depth_left": 1
  }
}
```

**推理证据 Trace 扩展**：
```json
{
  "reasoning_evidence": {
    "type": "graph_based",
    "evidence_type": "path_based",
    "paths": [
      [
        { "head": "Beijing", "relation": "capital_of", "tail": "China" },
        { "head": "China", "relation": "located_in", "tail": "Asia" }
      ]
    ],
    "confidence_scores": [0.94, 0.87],
    "backend_used": "gfm-retriever-v1",
    "subgraph_id": "sg-20251103-abc"
  }
}
```

**记忆操作 Trace 扩展**：
```json
{
  "memory_op_type": "state_set | kg_write | vector_store | profile_update",
  "memory_key": "travel.departure_date",
  "backend_used": "context | graphiti | qdrant | mem0",
  "latency_ms": 12,
  "user_id": "user_123"
}
```

**对话节点 Trace**：
```json
{
  "conversation": {
    "topic_id": "booking",
    "role_id": "agent",
    "turn": 3
  }
}
```

**记录规则**：
- 仅当调用图原生接口时记录
- 所有字段均为可选，执行器按能力填充
- `backend_used` 必须记录实际使用的后端标识，便于调试

### 7.4 标准库版本与依赖管理
- **路径支持语义化版本**：`/lib/...@v1`
- **子图可声明依赖**：`requires: - lib: "/lib/reasoning/...@^1.0"`
- **执行器启动时解析依赖图**，拒绝循环或缺失依赖
- **签名变更策略**：
  - `stable` 子图仅可增加字段，不可删除/修改类型
  - 签名变更需提升主版本号
- **小版本升级**（3.x → 3.y）保证向后兼容

### 7.5 归档与签名强制
- `archive_to("/lib/...")` 必须附带有效 `signature`，否则拒绝归档（`ERR_SIGNATURE_REQUIRED`）
- 归档目标路径必须符合命名空间规则
- 归档操作需记录完整 Trace，包括源子图ID、操作者、时间戳

## 八、核心能力规范

### 8.1 动态 DAG 执行 + 全局预算
**DAG 启动流程**：解析 → 验证资源 → 验证签名 → 检查入口 → 启动调度器

**`execution_budget`**：`max_nodes`, `max_subgraph_depth`, `max_duration_sec`  
**超限** → 跳转 `/__system__/budget_exceeded`  
**终止条件**：队列空 + 无活跃生成 + 无待合并子图 + 预算未超

### 8.2 动态子图生成
- LLM 必须输出 `### AgenticDSL '/dynamic/...'` 块
- 新子图可被后续节点通过 `next: "/dynamic/plan_123"` 调用
- **禁止行为**：LLM 生成的子图不得包含 `/lib/**` 写入或调用未声明工具
- 动态子图必须通过运行时权限检查

### 8.3 并发与依赖表达
- `wait_for` 支持 `any_of` / `all_of`
- 支持动态依赖：`wait_for: "{{ dynamic_branches }}"`
- **依赖解析时机**：节点入调度队列前
- **禁止**：在执行中动态变更依赖拓扑

### 8.4 自进化控制
```yaml
on_success: archive_to("/lib/solved/{{ problem_type }}@v1")
```
- 成功 DAG 自动存入图库
- **归档目标可为任意路径，但仅 `/lib/**` 被视为标准库**
- 归档必须提供有效签名

### 8.5 开发模式
```yaml
mode: dev | prod
```

- **`dev`**：`signature_validation: warn`，允许 `last_write_wins`，含上下文快照
- **`prod`**（默认）：强制 `strict`，禁用 `last_write_wins`，最小权限沙箱启用
- **Trace 增强**：`dev` 模式下包含快照信息（若 budget 允许）

### 8.6 性能边界指南
- **上下文大小**：<1MB（>512KB 启用快照压缩）
- **单子图节点数**：<50
- **预算建议**：`max_nodes: 10 × [预期分支数]`，`max_subgraph_depth: 3`
- **记忆操作**：单次查询响应时间 <100ms

### 8.7 Context 快照机制
```yaml
type: assign
assign:
  expr: "{{ $.ctx_snapshots['/main/step3'] }}"  # ✅ 静态键
  path: ""
```

⚠️ **安全限制**：`$.ctx_snapshots` 的访问键必须为静态字符串，禁止动态计算（如 `{{ $.key }}`）

## 九、LLM 生成指令
> 你是一个推理与行动架构师，你的任务是生成可执行、可验证的动态 DAG，包含：
> - 行动流：调用工具、与人协作
> - 思维流：假设 → 计算 → 验证
> 
> 你必须：
> 1. 输出一个或多个 `### AgenticDSL '/path'` 块
> 2. 遵守预算：递归深度 ≤ `{{ budget.subgraph_depth_left }}`
> 3. 优先调用标准库（清单含 `signature`）
> 4. 所有 LLM 调用必须包含 `seed` 与 `temperature`
> 5. 优先调用 `/lib/dslgraph/generate@v1` 生成新子图
> 
> 可用库清单（含契约）：
> {% for lib in available_subgraphs %}
> - {{ lib.path }} (v{{ lib.version }}): {{ lib.description }}
>   Inputs: {{ lib.signature.inputs | map(attr='name') | join(', ') }}
>   Outputs: {{ lib.signature.outputs | map(attr='name') | join(', ') }}
> {% endfor %}
> 
> 当前上下文：
> - 已执行节点：`{{ execution_context.executed_nodes }}`
> - 任务目标：`{{ execution_context.task_goal }}`
> - 执行预算剩余：`nodes: {{ budget.nodes_left }}, depth: {{ budget.subgraph_depth_left }}`
> - （训练模式）期望输出：`{{ expected_output }}`
> - 可用资源声明：`{{ available_resources }}`

## 十、标准原语层

### 10.1 子图管理（`/lib/dslgraph/**`）
- `/lib/dslgraph/generate@v1`（stable）

### 10.2 推理原语（`/lib/reasoning/**`）
- `/lib/reasoning/hypothesize_and_verify@v1`
- `/lib/reasoning/stepwise_assert@v1`
- `/lib/reasoning/counterfactual_compare@v1`（experimental）
- `/lib/reasoning/try_catch@v1`
- `/lib/reasoning/induce_and_archive@v1`
- `/lib/reasoning/graph_guided_hypothesize@v1`（experimental）
- `/lib/reasoning/iper_loop@v1`
- `/lib/reasoning/generate_text@v1`
- `/lib/reasoning/structured_generate@v1`
- `/lib/reasoning/continue_from_kv@v1`
- `/lib/reasoning/stream_until@v1`
- `/lib/reasoning/speculative_decode@v1`（experimental）
- `/lib/reasoning/fallback_text@v1`
- `/lib/reasoning/fallback_structured@v1`

### 10.3 内存记忆原语（`/lib/memory/**`）

> **详细内容已迁移到 [`docs/specs/memory-v3.10.md`](memory-v3.10.md)**，本节保留作为快速参考列表（Stage 2 / Task 9 整合自 `memory.md` (MEP-001 v3.2 Draft) + 本节 v3.10）。

- `/lib/memory/state/set@v1`
- `/lib/memory/state/get_latest@v1`
- `/lib/memory/kg/query_subgraph@v1`
- `/lib/memory/kg/write_subgraph@v1`
- `/lib/memory/vector/store@v1`
- `/lib/memory/vector/recall@v1`
- `/lib/memory/profile/update@v1`
- `/lib/memory/profile/get@v1`

**权限模型**：完整表见 [`docs/specs/memory-v3.10.md`](memory-v3.10.md#权限模型汇总)。

**工具注册要求**：
| 工具名 | 输入 | 输出 | 参考实现 |
|--------|------|------|----------|
| vector_store | text, metadata | success | Pinecone/Qdrant |
| vector_recall | query, top_k, filter | memories | Pinecone/Qdrant |
| profile_update | user_id, attributes | success | Redis/MongoDB |

### 10.4 对话协议（`/lib/conversation/**`）
- `/lib/conversation/start_topic@v1`（stable）
- `/lib/conversation/switch_role@v1`（stable）
- `/lib/conversation/meeting@v1`（stable）

**对话上下文模型**（10.4.1）：
- 每个话题拥有独立上下文路径：`/topics/{topic_id}/context`
- 角色切换更新 `conversation.current_role`
- 会议协调器管理多角色状态同步
- 预算控制：`max_conversation_turns`、`max_topics`、`max_roles`

### 10.5 工作流原语（`/lib/workflow/**`）
- `/lib/workflow/parallel_map@v1`（experimental）

### 10.6 世界模型及环境感知原语
*待定义：AgenticDSL 感知物理世界的原语*

### 10.7 资源工具
- `/lib/tool/list_available@v1`：动态查询当前可用工具及其能力标签，供 LLM 规划使用

## 附录 A：应用组织模型（工程推荐）

### A.1 应用目录结构推荐
```
my_project/
├── app/
│   └── my_robot/
│       ├── main.agent.md        # entry_point: "/app/my_robot/main"
│       └── private_utils.agent.md
├── lib/
│   └── workflow/
│       └── navigation/
│           └── path_planner@v1.agent.md
└── README.md
```

### A.2 演进路径：从私有到共享
1. **开发阶段**：逻辑置于 `/app/<AppName>/xxx`
2. **验证成功**：通过 `iper_loop` 或人工确认效果
3. **归档发布**：调用 `archive_to("/lib/workflow/...@v1")`
4. **复用阶段**：其他应用通过 `next: "/lib/workflow/...@v1"` 调用

**注意**：`/app/**` 在 DSL 层面与 `/main/**` 语义等价，规范强制要求执行器支持该路径。

## 附录 B：错误码
| 错误码 | 含义 |
|--------|------|
| ERR_MISSING_ENTRY_POINT | 未声明 entry_point |
| ERR_NAMESPACE_VIOLATION | 违反命名空间写规则 |
| ERR_CTX_MERGE_CONFLICT | 上下文合并冲突 |
| ERR_RESOURCE_UNAVAILABLE | 资源声明验证失败 |
| ERR_UNSUPPORTED_CAPABILITY | 请求的能力未被支持 |
| ERR_SIGNATURE_VIOLATION | 子图签名验证失败 |
| ERR_BUDGET_EXCEEDED | 超出执行预算（节点数、深度或时间） |
| ERR_SIGNATURE_REQUIRED | 归档至 `/lib/**` 时缺少签名 |

## 附录 C：核心标准库清单
以下子图为强制实现的核心标准库（执行器必须内置）：

### C.1 子图管理
- `/lib/dslgraph/generate@v1`

### C.2 推理原语
- `/lib/reasoning/generate_text@v1`
- `/lib/reasoning/structured_generate@v1`
- `/lib/reasoning/try_catch@v1`
- `/lib/reasoning/hypothesize_and_verify@v1`

### C.3 内存记忆原语
- `/lib/memory/state/set@v1`
- `/lib/memory/state/get_latest@v1`
- `/lib/memory/kg/query_subgraph@v1`
- `/lib/memory/vector/recall@v1`

### C.4 对话协议
- `/lib/conversation/start_topic@v1`
- `/lib/conversation/switch_role@v1`

## 附录 E：最佳实践与约定

### E.1 时间上下文约定（非强制）
- `$.now`: ISO8601 当前时间（由执行器注入）
- `$.time_anchor`: 任务参考时间点
- `$.timeline[]`: `{ts: "...", event: "...", source: "..."}`

### E.2 禁止行为清单
- 在 DAG 内实现异步回调
- 在叶子节点中编码高层推理逻辑
- 使用 `generate_subgraph` 调用已有子图
- 输出非 `### AgenticDSL` 块的 LLM 内容
- 在生产模式下使用 `last_write_wins` 合并策略
- 在知识应用层直接使用 `llm_generate_dsl`
- 在 `/lib/dslgraph/**` 之外实现子图生成逻辑

### E.3 推荐开发工作流
1. `agentic validate example.agent.md`
2. `agentic simulate --mode=dev`
3. 从 Trace 提取失败案例，更新 `expected_output`
4. 通过 `archive_to` 沉淀验证通过模块
5. 生产部署必须显式设置 `mode: prod`

### E.4 资源声明最佳实践
- 所有对外部能力的依赖（工具、运行时、网络）应在 `/__meta__/resources` 中显式声明
- 避免在 `generate_subgraph` 生成的子图中使用未声明资源
- 生产环境必须完整声明资源，开发环境可适当放宽（但不推荐）

## 附录 F：记忆原语演进路线
- **6 个核心子图**（`set`, `get_latest`, `store`, `recall`, `update`, `get`）
- **实验性**：
  - `/lib/memory/orchestrator/hybrid_recall@v1`（融合结构化+语义）
  - 支持记忆 TTL（`assign` + `$.now` + 过期策略）
  - 多模态记忆存储（图像、音频、视频）

## 附录 G：适配层参考实现指南
*本附录仅提供参考实现模式，不强制要求。执行器可自由选择实现细节，只要符合接口契约。*

### G.1 工具适配器示例
```python
# 参考伪代码
class ToolAdapter:
    def __init__(self, tool_name, schema):
        self.tool_name = tool_name
        self.schema = schema  # 符合 tool_schema 规范
        
    def validate_input(self, args):
        # 使用 JSON Schema 验证
        pass
        
    def execute(self, args, permissions):
        # 权限检查
        if not self.check_permissions(permissions):
            raise PermissionError(f"Missing permissions for {self.tool_name}")
            
        # 执行工具
        result = self._tool_impl(args)
        
        # 生成 Trace
        trace = {
            "tool_name": self.tool_name,
            "latency_ms": time.time() - start_time,
            "backend_used": self.backend_id
        }
        
        return result, trace
```

### G.2 C++ 推理核心集成点

> **C1 后状态（2026-06-08）**：参考执行器已从 `ILLMTool` + `LlamaAdapter` 直接集成
> 迁移到 `ILLMProvider` + `IGenerationStream` 流式接口（ADR-0001）。
> 旧式 `ILLMTool::generate(prompt, LLMParams)` 路径保留为向后兼容，但新增
> provider 应实现 `ILLMProvider`。
> `LLMParams` 现为 `LLMConfig` 的类型别名（`llm_types.h`），二者字段集完全一致。

```cpp
// 1. ILLMProvider 接口（src/common/llm/llm_types.h, C1 标准）
class ILLMProvider {
public:
    virtual ~ILLMProvider() = default;
    virtual Result<GenerationResult, LLMError>
        generate(const GenerationRequest& req, std::stop_token token) = 0;
    virtual std::unique_ptr<IGenerationStream>
        generate_stream(const GenerationRequest& req, std::stop_token token) = 0;
};

// 2. 内置实现：LlamaAdapter（src/common/llm/llama_adapter.h, 旧 API）
class LlamaAdapter {
public:
    struct Config { /* 同前 */ };
    explicit LlamaAdapter(const Config& config);
    std::string generate(const std::string& prompt);  // 同步，throw on error
    bool is_loaded() const;
};

// 3. C1 适配器：LlamaAdapterProvider（src/common/llm/llama_adapter_provider.h）
//    把同步 throw-on-error 的 LlamaAdapter 适配为流式 ILLMProvider
class LlamaAdapterProvider : public ILLMProvider {
public:
    explicit LlamaAdapterProvider(std::unique_ptr<LlamaAdapter> adapter);
    explicit LlamaAdapterProvider(const LlamaAdapter::Config& config);
    // 实现 generate() — 捕获 std::exception 转换为 LLMError
    // 实现 generate_stream() — 错误流通过 IGenerationStream::error() 传递
};

// 4. 模拟实现：MockLLMProvider（src/common/llm/mock_provider.h, 默认）
class MockLLMProvider : public ILLMProvider {
    // 支持队列/固定响应/错误注入/延迟模拟；CI 默认使用
};
```

**集成流程（C1 后）**：
1. 选择或实现一个 `ILLMProvider`（或使用内置 `MockLLMProvider` / `LlamaAdapterProvider`）
2. 通过 `DSLEngine::set_llm_provider(std::unique_ptr<ILLMProvider>)` 注入
3. DSL 中 `dsl_call` / `generate_subgraph` 节点无需 `llm_tool_name`，直接使用注入的 provider
4. 执行器（`NodeExecutor` 持有 `ILLMProvider*`）通过 `ILLMProvider::generate_stream()` 调用，
   通过 `IGenerationStream::error()` 获取错误

**向后兼容路径**：旧式 `engine->register_llm_tool(name, tool)` + `ILLMTool::generate(prompt, LLMParams)` 
仍可工作（`llm_tool.h` / `llama_tool.h` 保留），但**新增代码应使用 `set_llm_provider`**。

**能力扩展**：通过 `ResourceManager` 注册 `Resource`（类型 `CUSTOM`，`metadata["capabilities"]` 声明能力列表），供节点权限检查使用。

---

**AgenticDSL v3.10 是参考执行器 v1.0 对应的稳定规范版本**。  
通过 **三层抽象 + `dsl_call`/`generate_subgraph` 统一节点模型 + `ILLMTool` 接口解耦**，  
为构建 **可靠、可协作、可进化的智能体生态** 提供工业级工程基石。

**参考执行器 v1.0** 已开源，包含完整的 DAG 调度器、上下文引擎、LLM 工具注册机制及 Trace 导出。  
**v1.x 计划**：真正并发执行（Fork/Join）、对话协议完整实现、Context TTL、`codelet_call` 沙箱支持。

---

## 附录 C: 需求可追溯矩阵 (REQ Traceability)

> **治理规则**: 每个来自 Approved ADR 的需求必须在此处有对应的 REQ-XXX 条目。
> 每当 ADR Approved，ADR Sponsor 必须在 1 周内在此补充 REQ。
> Drift Gate (每 2-3 Sprint) 验证 "Approved ADR 数量 == 此处引用 ADR 数量"。

### REQ-CTX-001: Context 必须支持 5 层结构 (L1-L5)

- **来源**: ADR-0008 §决策 1
- **行为**: `LayeredContext` 提供 L1(系统) / L2(会话) / L3(认知) / L4(领域) / L5(工具) 五层隔离
- **验证**: `tests/test_layered_context.cpp::test_five_layers_access`
- **状态**: ✅ 已实现 (2026-06-12)

### REQ-DAG-001: 引擎必须支持 DAG 拓扑调度

- **来源**: ADR-0020 §决策 2
- **行为**: `TopoScheduler` 按拓扑顺序调度节点执行，支持 `fork`/`join` 分支合并
- **验证**: `tests/test_scheduler.cpp::test_topo_order` + `test_fork_join_basic`
- **状态**: ✅ 已实现

### REQ-TOOL-001: 工具注册表必须支持分层权限

- **来源**: ADR-0004 §决策 3
- **行为**: `ToolRegistry` 注册工具时验证 `ToolMetadata` 中的 `allowed_layers` 和 `approval_policy`
- **验证**: `tests/test_tool_registry.cpp::test_layer_permission_check`
- **状态**: ✅ 已实现 (2026-07-02)

### REQ-DSL-001: `dsl_call` 必须支持 Markdown DSL 子图调用

- **来源**: DSL v3.10 规范 §5.9
- **行为**: `DSLEngine::from_markdown()` 解析 Markdown DSL → `ParsedGraph`，通过 `dsl_call` 节点调用子图
- **验证**: `tests/test_basic.cpp::test_dsl_call_simple`
- **状态**: ✅ 已实现

### REQ-SKILL-001: SKILL.md 必须在隔离子进程中执行

- **来源**: ADR-0055 §决策 1
- **行为**: `SkillInterpreter::run()` 通过 `posix_spawn` + `seccomp(BPF)` + `pipe IPC` 执行 SKILL.md
- **验证**: `tests/test_skill_interpreter.cpp::test_basic_execution`
- **状态**: ✅ 已实现 (2026-07-22)

### REQ-W5-001: 节点可声明执行环境 (backend: 字段)

- **来源**: ADR-0072 D4 + ADR-0075 EnvBackend
- **行为**: 节点 YAML 可声明 `backend:`（值 ∈ {"local", "docker", 自定义 backend 名}）和 `env_vars:`（key→string 映射）。解析后存入 `Node::metadata["backend"]` / `Node::metadata["env_vars"]`，供运行时 `ToolCoordinator` + `EnvValidationHook` 读取并应用 BackendPolicy。
- **向后兼容别名**: `env:` 字段作为 `env_vars:` 的旧名（向后兼容既有 DSL 写法）。两者并存时 `env_vars:` 优先。
- **未知 backend 容错**: 未知 backend 名（如 `backend: custom_xyz`）不抛解析异常，仅存入 metadata，由运行时 `EnvValidationHook` 决策。
- **示例**:

  ```yaml
  - id: my_docker_task
    type: tool_call
    tool: shell/exec
    arguments:
      cmd: "echo hello"
    output_keys: ["result"]
    backend: docker
    env_vars:
      DB_HOST: localhost
      DB_PORT: "5432"
  ```

- **验证**: `tests/test_dsl_extensions.cpp::test_dsl_extensions[W5]`
- **状态**: ✅ 已实现 (2026-09-03, Sprint 25 Change #3) — ADR-0072 D4 实施度从 1/6 升至 2/6

### REQ-W4-001: 节点可声明流式执行模式 (stream: 字段, 阶段 A 仅透传)

- **来源**: ADR-0072 D1 (阶段 A 解析层, 阶段 B IStreamHandle 语义留 Sprint 26)
- **行为**: 节点 YAML 可声明 `stream: true` / `stream: false`，解析后存入 `Node::metadata["stream"]`。**阶段 A 仅字段透传**，运行时流式行为由阶段 B (`IStreamHandle` 新契约 + 3 类节点 tool_call / shell.exec / dsl_call 运行时实现 + stop_token 传播交互) 实施。
- **缺省语义**: 不含 `stream` 字段时 `Node::metadata` 不含 `stream` key（区别于显式 `stream: false`）
- **示例**:

  ```yaml
  - id: log_tail
    type: tool_call
    tool: shell/exec
    arguments:
      cmd: "tail -f /var/log/syslog"
    output_keys: ["log_line"]
    stream: true
  ```

- **验证**: `tests/test_dsl_extensions.cpp::test_dsl_extensions[W4]`
- **状态**: ✅ 已实现阶段 A (2026-09-03, Sprint 25 Change #4) — ADR-0072 D1 实施度从 0/6 升至 1/6（仅字段层）；阶段 B 留 Sprint 26

### REQ-W4-001-B: 运行时流式执行语义 (阶段 B, 2026-09-04 ship)

- **来源**: ADR-0072 D1 阶段 B (IStreamHandle 运行时语义, Sprint 26 W4 阶段 B)
- **行为**:
  - `NodeExecutor::set_stream_sink(IStreamHandle*)` 注入点 (raw pointer, no ownership, 与 set_tool_coordinator 一致)
  - 当 `metadata["stream"] == true` (严格 JSON 布尔) 且 `stream_sink_` 已注入时, 节点 (tool_call / dsl_call) 同步执行完成后将 ToolResult/Context 切片为 N 个 chunk (kStreamChunkSize=64), 通过 `sink_->push()` 写入, 最后 `sink_->close(nullopt)` 标记 EOF
  - 消费者通过 `sink_->next(token)` 拉取; cancel 通过持 stop_source → `next(token)` 内部检查 stop_requested() 终止
  - **V1 语义**: 节点仍走同步执行路径, 流式触发指完成后切片回放 (post-hoc pseudo-streaming). 真流式 (incremental) 依赖 IToolRegistry streaming API, V2 阶段交付
  - **scope**: 2 类节点 (tool_call / dsl_call); shell_exec NodeType 不存在 (W5 parser 提案)
- **触发条件**:
  - `metadata["stream"] == true` (JSON 布尔) AND `stream_sink_ != nullptr` → 流式分支
  - `metadata["stream"] == true` AND `stream_sink_ == nullptr` → 警告 stderr + 同步路径
  - 缺省 / false / 字符串 / 数字 / null → 同步路径 (向后兼容, 既有 100+ 测试零回归)
- **契约层**: `include/agenticdsl/contract/i_stream_handle.h` (5 虚函数: 3 consumer pull + 2 producer push/close)
- **参考实现**: `src/common/runtime/stream_handle.{h,cpp}` (`BufferedStreamHandle` pull + `CallbackStreamHandle` push+pull, RAII 析构顺序)
- **验证**: `tests/test_stream_runtime.cpp` (5 类 TEST_CASE / 19 cases, 15 PASS, 4 集成测试已知限制待 follow-up)
- **状态**: ✅ 已实现阶段 B (2026-09-04, Sprint 26 W4 阶段 B) — ADR-0072 D1 实施度从 1/6 升至 2/6
