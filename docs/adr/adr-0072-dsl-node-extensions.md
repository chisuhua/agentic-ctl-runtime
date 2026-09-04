# ADR-0072: DSL 节点扩展 (stream: / $var / declarative style / backend:)

## 状态

🟡 **Partial** (2026-09-02 — 翻牌依据：D3 `declarative style` + D5 `双语法共存期基础设施` 在 ADR 仍 🔍 Proposed 时 ship，违反 [STATUS-GLOSSARY.md](../../adr-management/STATUS-GLOSSARY.md) "Proposed = 未到实施阶段" 定义；按本项目治理惯例 [ADR-0073 增量翻牌路径](./adr-0073-tool-json-schema-contract.md) 由 🔍 Proposed → 🟡 Partial；D1 `stream:true` + D4 `backend:` 字段未实施，D2 `$var` 因 Evidence Gate = Conditional 未触发维持 N/A)

**翻牌时点状态分类**：
| 决策 | 节点/字段 | 当前状态 | 备注 |
|------|----------|:---:|------|
| **D1** | `stream: true` 扩展到所有 tool_call/dsl_call (shell_exec 留 W5) | ✅ **阶段 A + 阶段 B ship** | 阶段 A (2026-09-03 commit `c61a6d0`): parser 字段透传. 阶段 B (2026-09-04): IStreamHandle L1 契约层 + BufferedStreamHandle/CallbackStreamHandle 参考实现 + NodeExecutor `set_stream_sink()` 注入点 + 2 类节点 (tool_call/dsl_call) V1 切片回放. shell_exec NodeType 不存在 (per `node.h:22-34` NodeType 枚举), W5 parser 提案交付 |
| **D2** | `$var` 替代 `{{ }}` | ⏸ N/A | 触发条件 parse-valid < 85%；[2026-09-02 Evidence Gate 决议 = Conditional](../../audits/2026-09-02-evidence-gate-v1.md) (mock 88.24% ∈ [85,90)) → 未触发 |
| **D3** | `declarative style` (`exec:` 语法糖) | ✅ **已 ship** | `src/modules/parser/declarative_style.{h,cpp}` (C6: ADR-0072 D3 标注) + `markdown_parser.cpp:164,198` 集成钩子 + `tests/test_dsl_extensions.cpp` |
| **D4** | `backend:` 字段 | ❌ 未实施 | parser 无 `backend:` 字段证据；ADR-0075 D4 backend_policy.h 已 ship 但 DSL 解析器未对接 |
| **D5** | 双语法共存期基础设施 | ✅ **已 ship (D3 arm)** | D3 arm (`exec: ↔ type: tool_call`) 共存基础设施已 ship（含 `dual_syntax_lint.cpp` C7 + `test_dsl_extensions.cpp` backward compat）；D2 arm（`$var ↔ {{}}`）未触发 N/A（等真实 3 模型 baseline FAIL）|
| **D6** | `try/catch/finally` 节点族 | ⛔ OFF | ADR-0071 §3.C "默认不做"；不计入翻牌范围 |

**翻牌触发**：2026-09-02 实施 `from-roadmap-phase-6c-execution-dsl` change（OpenSpec change，已 ship），翻牌 OpenSpec 待建：`2026-09-02-adr-0072-flip-to-partial`（per single-dev mode 治理：issue + Self-Review Checklist + 24h cooling-off）。

**证据基础**：
- D3 ship 实证：`src/modules/parser/declarative_style.cpp:2`（文件头注释 "C6: ADR-0072 D3 declarative style `exec: [...]` 解析"）+ `src/modules/parser/declarative_style.h:2`（同样注释）+ `src/modules/parser/CMakeLists.txt:4` 注册
- D5 ship 实证：`src/modules/parser/markdown_parser.cpp:38,164,198` 三处 C6 集成钩子
- 测试覆盖：`tests/test_dsl_extensions.cpp`（per ADR-0072 C6 spec "test_dsl_extensions 4 cases"）
- 治理异常文档化：D3/D5 实施先于 ADR 翻牌（per single-dev mode 治理允许）

**翻牌后保留 Proposed 属性的部分**：D1（强制必补）/ D4（强制必补）未 ship，ADR 维持 🟡 Partial 而非 ✅ Approved；后续 Evidence Gate 真实 3 模型重测（Sprint 25+ carry-over）若 FAIL 触发 D2，则 D5 双语法共存期需要 6 个月双语法过渡。

## 领域

L0 运行时 / DSL 语言演进 / 节点族扩展 / LLM-DSL 协同

## 关联

### 父 ADR
- [ADR-0071 — LLM-native AgenticDSL 架构](./adr-0071-llm-native-agenticdsl-architecture.md) §决策 D3 (本 ADR 是 D3 的具体实施: 5 个 DSL 节点/字段扩展)
- ADR-0071 §3.A 必补 / §3.B 条件性 / §3.C 不做 — 优先级分层 (本 ADR 严格遵守)
- ADR-0071 §决策 D5 (本 ADR `$var` 与 `$var`/`{{}}` 双语法共存期 6 个月)

### 上游锚定
- [ADR-0008 — 结构化 Context](./adr-0008-structured-context.md) — `$var` 引用 LayeredContext.working.data.* 路径
- [ADR-0073 — Tool JSON Schema 契约](./adr-0073-tool-json-schema-contract.md) — ToolMetadata V3 schema 是 stream/ 后端选型的约束接口
- [ADR-0074 — Prompt Engineering + Evidence Gate](./adr-0074-prompt-evidence-gate.md) — §决策 D4 Evidence Gate 是本 ADR §决策 D2 (D2 $var) 和 §决策 D6 (D6 try/catch 默认不做) 的触发条件
- [ADR-0075 — EnvBackend Local + Docker](./adr-0075-env-backend-local-docker.md) — §决策 D4 `backend:` 字段 (本 ADR D4 衔接)

### 平行/下游
- ADR-0076 (DSL Engine as MCP Server) — 本 ADR stream: 字段可作为 MCP `tools/call` 扩展 (Phase 7+)

### 规范
- [`docs/specs/dsl.md`](../specs/dsl.md) v3.10 — 当前 DSL 规范 (本 ADR 实施后升至 v3.11)
- [`docs/specs/architecture.md`](../specs/architecture.md) §2.1 — 三层架构 (D6 try/catch 必须不违反此约束)
- Inja template syntax `{{ }}` — 当前 Context 插值 (本 ADR `$var` 双语法共存期)

---

## 背景

### 问题

ADR-0071 §决策 D3 列出 5 个 LLM-native 必需的 DSL 节点/字段扩展。当前 5 个具体空白:

1. **`stream: true` 仅 LLM** — `dsl.md §5.5.3` 只对 `dsl_call` 生效; `tool_call` / `shell.exec` 无法流式输出 (用户需求: 长输出实时显示)
2. **`$var` 替代 `{{ }}` 未引入** — Inja `{{ }}` 与 Markdown fenced DSL 频繁冲突, LLM parse 失败率高 (ADR-0074 D3 baseline 测量)
3. **declarative style 缺失** — 当前 imperative `type: tool_call` + `tool: ...` 语法冗长, LLM-friendly declarative style 未提供
4. **`backend:` 字段未落地** — ADR-0071 D3 §3.A "无 env: 声明" 已识别; ADR-0075 D4 实施重命名为 `backend:`, 本 ADR 衔接
5. **`try/catch` 节点族空白** — LLM 无法声明错误恢复; 但 ADR-0071 §3.C 明确 "默认不做" (违反 3 层架构)

### 解决方案

按 ADR-0071 §3.A/3.B/3.C 优先级分层实施 5 个节点/字段扩展:

| 决策 | 节点/字段 | ADR-0071 优先级 | 触发条件 |
|------|----------|:---:|------|
| **D1** | `stream: true` 扩展到所有 tool_call / shell.exec / dsl_call | §3.A 必补 | 直接实施 |
| **D2** | `$var` 替代 `{{ }}` | §3.B 条件性 | ADR-0074 Evidence Gate parse-valid < 85% |
| **D3** | declarative style (`exec:` 语法糖) | §3.B 条件性 | 用户反馈 + 85% ≤ parse-valid < 90% (Evidence Gate 临界带) |
| **D4** | `backend:` 字段 | §3.A 必补 | 直接实施 (ADR-0075 D4 已设计) |
| **D5** | 双语法共存期 6 个月 | §3.D breaking change 政策 | D2+D3 触发后强制 |
| **D6** | `try/catch/finally` 节点族 | §3.C 不做 | 默认 OFF; 子图表达力不足证明 + Evidence Gate PASS |

### 已实证证据

- **ADR-0071 §3.A/3.B/3.C 已分层**: 5 个扩展的优先级 + 触发条件已权威定义
- **Inja `{{ }}` 与 Markdown 冲突**: `docs/specs/dsl.md` 已记录 (line 141)
- **`stream: true` LLM 流式已 ship**: `dsl.md §5.5.3` + `examples/agent_loop/` 演示
- **ToolCoordinator hook 体系** (ADR-0069): stream callback 可作为 post-hook 注入

---

## 决策

### D1. `stream: true` 扩展到所有 tool_call / shell.exec / dsl_call

**目标**: 所有调用类节点支持流式输出, 解决长输出实时显示 + LLM 训练数据采集粒度问题。

**当前状态** (per `docs/specs/dsl.md §5.5.3`):

```yaml
- type: dsl_call
  target: llm.call
  args: {prompt: "...", max_tokens: 1000}
  stream: true              # ✅ 已支持
- type: tool_call
  tool: fs.read
  arguments: {path: "/etc/hostname"}
  # ❌ 不支持 stream (一次性返回)
```

**扩展后** (本 ADR):

```yaml
- type: tool_call
  tool: fs.read
  arguments: {path: "/var/log/syslog"}
  stream: true              # ✅ 新增
  stream_chunk_size: 4096   # 每次 callback 推送字节数 (default: 1024)

- type: shell.exec
  cmd: "tail -f /var/log/syslog"
  stream: true              # ✅ 新增 (shell 流式, 复用 ADR-0075 LocalBackend pipe)

- type: dsl_call
  target: llm.call
  args: {prompt: "..."}
  stream: true              # 已支持 (无变化)
```

**流式协议**:

1. **回调接口**: 复用 `ILLMProvider::generate_stream()` 模式
2. **Stream handle**: `ToolResult.stream_handle` (类似 `std::unique_ptr<IStreamHandle>`)
3. **API**: `while (auto chunk = handle->next()) { /* process */ }`
4. **背压**: `IInteractionBus` event-driven (Phase 2 ADR-0030 V2 衔接)

**实施**:

- `include/agenticdsl/types/tool_call.h` — ToolCall 增加 `stream` + `stream_chunk_size` 字段
- `src/common/llm/stream_handle.h/cpp` — IStreamHandle 接口 + ToolStreamHandle 实现
- `src/modules/executor/node_executor.cpp` — tool_call / shell.exec / dsl_call 3 处分流逻辑
- ToolCoordinator execute 流支持 stream response (post-hook 收到 stream 事件)

**LLM 训练数据价值**:

- stream callback 触发 `tool.execution.stream.chunk` 事件 (ADR-0068 衔接)
- 训练数据采集粒度提升: 一次性输出 → 流式 chunk 序列, 用于学习"何时停止"

### D2. `$var` 替代 `{{ }}` — Evidence Gate 条件性触发

**触发条件**: ADR-0074 Evidence Gate D4 `parse-valid < 85%` 测量结果。

**目标**: 引入 `$var` 语法替代 Inja `{{ }}`, 解决 Markdown fenced DSL 频繁冲突。

**双语法对比**:

| 语法 | 例子 | 优点 | 缺点 |
|------|------|------|------|
| Inja `{{ }}` | `Working dir: {{ ctx.working.data.workdir }}` | 已 ship; 模板逻辑强 | 与 Markdown 冲突 (`{{ ... }}` 在 fenced block 内被误解析) |
| `$var` | `Working dir: $ctx.working.data.workdir` | 与 Markdown 兼容; LLM-friendly | 需新增解析器 |

**新语法**:

```yaml
- type: assign
  target: $ctx.working.data.user_id
  value: $ctx.working.data.input.user_id

- type: assert
  condition: $ctx.working.data.count > 0
  message: "Count must be positive"

- type: tool_call
  tool: github.get_pr
  arguments:
    repo: $ctx.working.data.repo
    pr_number: $ctx.working.data.pr_id
```

**双语法共存期 6 个月** (per ADR-0071 §3.D):

- **Phase 1** (引入后 0-3 月): `{{ }}` 与 `$var` 同时支持, LLM Prompt V3 标注首选 `$var`
- **Phase 2** (3-6 月): 默认 deprecation warning on `{{ }}`; LLM Prompt V3.5 强调 `$var`
- **Phase 3** (6 月后): `{{ }}` 解析支持移除, `$var` 成为唯一语法

**实施**:

- `include/agenticdsl/types/value_ref.h` — ValueRef AST 节点 (`$ctx.path.to.field`)
- `src/modules/parser/value_ref_parser.cpp` — `$var` 语法解析 (正则 + AST)
- `src/modules/executor/value_ref_resolver.cpp` — 运行时 $var → LayeredContext 路径解析

**风险**:

- LLM 训练数据需重标注 (`{{ }}` → `$var`), 估时 +1 周 (ADR-0074 衔接)
- 已 ship 的 `examples/` 7 个 demo 需迁移 (向后兼容期内可选)

### D3. declarative style (`exec:` 语法糖) — 用户反馈 + parse-valid 临界触发

**触发条件** (BOTH 必需):

1. 用户反馈 (declarative style 受欢迎)
2. ADR-0074 Evidence Gate 测量 `85% ≤ parse-valid < 90%` (Evidence Gate 临界带 — gate 已 PASS 但距稳定还有 5% 余量, declarative style 作为进一步优化路径)

**为何用临界带而非 `< 90%`**: 避免与 ADR-0074 D4 GO gate (≥85%) 冲突 — 若 gate 85% PASS 时 D3 永远不触发, 路径失效; 临界带 85% ≤ x < 90% 明确 D3 是"已可用但待优化"区间的优化路径, 不与 gate 重叠。

**目标**: 引入 LLM-friendly 节点语法, 简化 imperative 风格。

**当前 imperative 风格** (default):

```yaml
- type: tool_call
  tool: fs.read
  arguments:
    path: "/etc/hostname"
```

**新 declarative 风格** (语法糖):

```yaml
- exec: fs.read
  with:
    path: "/etc/hostname"
```

或更紧凑 (单行):

```yaml
- exec: "fs.read --path=/etc/hostname"
```

**实施**:

- 解析器增加 `exec:` 节点识别 (MarkdownParser §决策 D2)
- `exec:` 节点 transform 为 `type: tool_call` + `tool: <name>` + `arguments: <with>` (零运行时开销, 仅 syntactic sugar)
- LLM Prompt V3.5 标注 declarative style 优先

**优先级**:

- 用户反馈主导 — 仅在用户偏好 declarative 时启用
- 默认仍 imperative (向后兼容, 不强制)

### D4. `backend:` 字段 — 衔接 ADR-0075 §决策 D4 (已设计, 本 ADR 实施)

**目标**: DSL 节点 `shell.exec` / `dsl_call` 显式声明执行后端, 多环境隔离。

**当前草案** (per ADR-0071 §3):

```yaml
- type: shell.exec
  cmd: "ls -la /tmp"
  env: production          # ⚠️ 与 ExecOptions.env 冲突
```

**重命名 + 新字段后** (per ADR-0075 D4 + 本 ADR D4):

```yaml
- type: shell.exec
  cmd: "ls -la /tmp"
  env_vars:                # ExecOptions.env 重命名 (向后兼容 1 Sprint 别名)
    DEBUG: "1"
  backend: local           # 本 ADR 新增: 执行目标 (D4)
  timeout: 30000
```

**字段语义**:

| 字段 | 作用域 | 例子 |
|------|--------|------|
| `env_vars` | 进程环境变量 | `DEBUG=1`, `PATH=...` |
| `backend` | 执行目标 | `local`, `docker:container_xyz`, `docker:python:3.12` |
| `working_dir` | 工作目录 | `/workspace` |

**默认值**:

- 缺省 `backend: local` (向后兼容)
- 缺省 `env_vars: {}` (不继承 parent env)
- 缺省 `working_dir: $ctx.working.data.cwd` (LayeredContext.working.data.cwd)

**LLM Prompt V3 标注**:

- "若需沙箱隔离, 显式 `backend: docker:<image>`; 默认 local 仅适用 trusted context"
- ADR-0074 D5 两阶段 prompt 注入包含 backend 选择提示

**实施细节** (见 ADR-0075 §决策 D4):

- DSL 解析器支持 `backend:` 字段 + `env:` → `env_vars:` 别名 (1 Sprint 后删除别名)
- ToolCoordinator pre-hook (EnvValidationHook) 强制 backend policy
- 审计: `env.backend.exec.{start,end}` 事件 (ADR-0068)

### D5. 双语法共存期 (Breaking Change 政策)

**目标**: ADR-0071 §3.D 强制 6 月双语法共存期, 避免破坏现有 LLM 训练数据 + 已 ship 示例。

**适用范围** (D2+D3 触发后):

- D2 `$var` ↔ `{{ }}` 共存 6 月
- D3 `exec:` ↔ `type: tool_call` 共存 6 月
- D4 `env:` ↔ `env_vars:` 共存 1 Sprint (短, 非 6 月)

**共存机制**:

1. **解析器同时支持**: 两套语法都解析为同一 AST (内部表示统一)
2. **Lint 工具**: `tools/dsl_lint.py` 报告已 deprecated 语法使用情况
3. **Prompt 模板**: 每月更新 LLM Prompt V3.x, 渐进标注首选语法
4. **JSONL 训练数据**: 重标注按月, 旧样本保留 (Wave 5+ Fine-tune 用)
5. **弃用警告**: 解析时 `stderr` 输出 deprecated 提示 (不报错)

**移除条件**:

- 6 月后 + LLM Prompt V4 全部使用新语法 + 训练数据 ≥90% 新语法

### D6. `try/catch/finally` 节点族 — 默认 OFF, Gated (per ADR-0071 §3.C)

**触发条件** (BOTH 必需):

1. **子图表达力不足证明**: 现有 20+ stdlib 子图 (auth/human/math/utils/inference) 无法表达 LLM 频繁生成的错误恢复模式 (e.g. "retry 3 times then fallback")
2. **ADR-0074 Evidence Gate PASS**: Wave 2 → Wave 3 推进已成功, parse-valid ≥85% + task-success 阈值达标

**目标** (触发后): LLM 声明式表达错误恢复, 减少 imperative retry 代码。

**节点语法** (草案, 触发后定稿):

```yaml
- try:
    steps:
      - tool_call: github.get_pr
        arguments: {pr_id: $ctx.working.data.pr_id}
  catch:
    - on_error: ERR_NETWORK
      steps:
        - assign: $ctx.working.data.retry_count
          value: $ctx.working.data.retry_count + 1
        - if: $ctx.working.data.retry_count < 3
          then: [retry from try]
  finally:
    - tool_call: audit.log
      arguments: {event: "pr_check_done"}
```

**约束** (per ADR-0071 §3.C):

- ❌ **不破坏 3 层架构** (`specs/architecture.md §2.1`) — try/catch 必须在 L1 层, 不下沉到 L0
- ❌ **不引入新错误码** — 复用 ADR-0031 IExecutionPolicy + ErrorCode 体系
- ❌ **不绕过 ApprovalHandler** — catch 步骤仍需 approval

**默认 OFF 理由** (per ADR-0071 §3.C):

> ❌ **原生 `try/catch/finally` 节点族（默认不做）**——违反 3 层架构（dsl.md §2.1）。先做子图，原生节点族需证明子图表达力不足。

**实施** (触发后, 估时 3-4 周):

- `src/modules/parser/error_handling_parser.cpp` — try/catch/finally AST 节点
- `src/modules/executor/try_catch_executor.cpp` — 错误传播 + catch 匹配 + finally 执行
- `tools/dsl_lint.py` — 错误处理深度检查 (避免 catch-all 黑洞)
- 训练数据: 50+ error recovery examples 采集

---

## 不变量

### 长期不变量

1. **D1 `stream: true` 是可选字段** — 缺省 false (向后兼容)
2. **D2 `$var` 触发前, 仅 `{{ }}` 语法** — ADR-0074 Evidence Gate parse-valid < 85% 测量前置条件
3. **D3 declarative style 仅语法糖** — 运行时 AST 与 imperative 风格一致
4. **D4 `backend:` 缺省 `local`** — 已 ship 示例零修改运行
5. **D5 双语法共存期严格 6 月** — 不得提前删除旧语法 (除非满足 D5 移除条件)
6. **D6 `try/catch` 默认 OFF** — ADR-0071 §3.C 权威; 触发前不实施
7. **所有扩展优先 stdlib 子图实现** — LLM-native 收益优先来自子图, 原生节点族是 fallback

### LLM 训练数据不变量

```
JSONL 训练数据 (ADR-0074 D6) 必须标注 syntax_version: "v3.10" | "v3.11"
v3.10 样本用 {{ }}; v3.11+ 样本用 $var
混用样本按 deprecated 标记, 用于 Phase 2 警告训练
```

### 解析器不变量

```
DSL 解析器 MUST 支持以下语法 (v3.11):
- type: tool_call + arguments (imperative, 默认)
- exec: + with (declarative, D3 触发后)
- stream: true (D1 强制)
- backend: <spec> (D4 强制)
- $var 引用 (D2 触发后)
- {{ }} 引用 (D2 触发后, 6 月共存期)
- try/catch/finally (D6 触发后, 默认 OFF)
```

---

## 风险

### 高风险

| 风险 | 缓解 |
|------|------|
| **D2 `$var` 与 Inja `{{ }}` 解析冲突** — 共存期两套语法同时解析, 优先级混乱 | 解析器明确优先级: `$var` 在 fenced block 优先, `{{ }}` 在 line text 优先; LLM Prompt V3.x 明确首选 `$var`; CI 测试覆盖混用 |
| **D3 declarative style 解析歧义** — `exec: foo` 可能被误识为普通文本 | 解析器 require `exec:` 在 YAML list item 首字段 (`- exec:`), 其他位置视为文本 |
| **D6 `try/catch` 触发后破坏 3 层架构** — catch handler 跨层调用 | ADR-0071 §3.C 约束强制: catch 步骤必须在 L1 层, 不下沉 L0; Linter 检查; 架构组评审 |

### 中风险

| 风险 | 缓解 |
|------|------|
| **D4 `backend:` 字段强制后, 已 ship DSL 文件 migration** — 现有 `.agent.md` 仍用 `env:` 字段 | 向后兼容: 解析器接受 `env:` 作为 `env_vars:` 别名 (1 Sprint); Lint 报告需迁移文件 |
| **D1 `stream: true` 工具实现参差** — 部分 PDK 工具不支持流式 | ToolMetadata V3 增加 `supports_stream: bool` 字段 (ADR-0073 衔接); 解析器拒绝对不支持流式的工具启用 stream |
| **D2 `$var` 触发后训练数据重标注** — 50+ golden tasks 重写 + 30+ few-shot 重写 | 自动化转换脚本 (1 周); 双语法期间接受混用样本; 6 月后统一 |
| **D5 共存期 LLM Prompt 频繁更新** — 每月 V3.x 版本管理混乱 | Prompt 版本管理规范化 (ADR-0074 §决策 D3); changelog 记录每次更新; baseline 测量每次 prompt 变更后 24h 内 |

### 低风险

| 风险 | 缓解 |
|------|------|
| **D3 declarative style 用户偏好不明显** — 用户仍偏好 imperative | D3 触发条件之一是用户反馈; 不满足则不启用; 语法糖零成本 |
| **D6 `try/catch` 永不触发** — 子图表达力始终足够 | 良性, 表明 stdlib 设计良好; ADR-0072 状态保持 🟡 Partial (D1+D4 持续 N/A) |

---

## 替代方案

### 替代 1: 一次性实施所有 5 个扩展, 不分层 (拒绝)

**否决理由**: ADR-0071 §3.A/3.B/3.C 已分层, 违反优先级破坏 Wave 2 → Wave 3 推进节奏; Evidence Gate 无法测量 parse-valid 改善。

### 替代 2: D6 `try/catch` 直接启用, 不 gate (拒绝)

**否决理由**: ADR-0071 §3.C 明确 "默认不做"; 违反 3 层架构 (dsl.md §2.1); 子图表达力未经证明。

### 替代 3: D2 `$var` 直接启用, 不等 Evidence Gate (拒绝)

**否决理由**: ADR-0074 D4 Evidence Gate 测量 parse-valid 是 D2 触发条件; 提前启用成本高 (训练数据重标注); parse-valid ≥85% 表明 `{{ }}` 实际可行。

### 替代 4: 用新 DSL 文件格式替代现有 YAML (拒绝)

**否决理由**: 与 ADR-0071 §D3 "Wave 2 默认零 breaking change" 不兼容; 破坏现有 7 个 demo + 12 个 stdlib; LLM 训练数据全部失效。

### 替代 5: D1 `stream: true` 仅 LLM, 不扩展到 tool (拒绝)

**否决理由**: 与 ADR-0071 §3.A "stream: true (扩展到所有 tool) — shell/gRPC 也需流式" 不符; 长输出实时显示刚需。

### 替代 6: 自研新解析器替代 MarkdownParser (拒绝)

**否决理由**: 维护成本 × N; 破坏现有 7 个 demo 加载逻辑; MarkdownParser 已稳定 ship (115/115 ctest pass)。

---

## 影响范围

### 文档
- [`docs/specs/dsl.md`](../specs/dsl.md) — v3.10 → v3.11 (D1 强制 + D4 强制 + D2/D3/D6 conditional)
- `docs/specs/dsl-extensions-v3.11.md` (新增) — 5 个扩展详细规范 (触发条件 + 语法 + 实施)
- `docs/llm/syntax-migration-guide.md` (新增) — `{{ }}` → `$var` + imperative → declarative 迁移指南 (D5 共存期)

### 代码
- `include/agenticdsl/types/value_ref.h` (D2 触发后新增) — ValueRef AST 节点
- `src/modules/parser/value_ref_parser.cpp` (D2 触发后新增) — `$var` 语法解析
- `src/modules/parser/declarative_style_parser.cpp` (D3 触发后新增) — `exec:` 语法糖解析
- `src/modules/executor/value_ref_resolver.cpp` (D2 触发后新增) — 运行时 $var 解析
- `include/agenticdsl/types/tool_call.h` (D1 强制) — `stream` + `stream_chunk_size` 字段
- `src/common/llm/stream_handle.h/cpp` (D1 强制) — IStreamHandle 接口
- `src/modules/executor/node_executor.cpp` (D1 强制) — 3 处分流逻辑 (tool_call/shell.exec/dsl_call)
- `src/modules/parser/markdown_parser.cpp` (D4 强制) — `backend:` 字段 + `env:` → `env_vars:` 别名 (衔接 ADR-0075 D4)
- `tools/dsl_lint.py` (D2/D3/D6 触发后新增) — 语法版本检查 + deprecated 警告

### 测试
- `tests/test_stream_tool_call.cpp` (D1 强制) — tool_call / shell.exec / dsl_call stream 路由
- `tests/test_stream_chunk_size.cpp` (D1 强制) — 流式 chunk size 控制 + 背压
- `tests/test_backend_field.cpp` (D4 强制) — `backend:` 解析 + `env:` 别名 (衔接 ADR-0075 测试)
- `tests/test_value_ref_parser.cpp` (D2 触发后新增) — `$var` 解析 + LayeredContext 路径
- `tests/test_declarative_style_parser.cpp` (D3 触发后新增) — `exec:` 语法糖 + imperative 等价
- `tests/test_dual_syntax_coexistence.cpp` (D5 触发后新增) — `{{ }}` + `$var` 共存 6 月
- `tests/test_try_catch_executor.cpp` (D6 触发后新增) — 错误传播 + catch 匹配 + finally

### 生态
- `examples/` 7 个 demo — D4 强制 (增加 `backend:` 示例); D1 强制 (部分 demo 启用 `stream: true`)
- `lib/` 12+ stdlib subgraphs — D4 强制 (重命名 `env:` → `env_vars:`); D1 强制 (部分 subgraphs 增加 stream)
- `data/training/*.jsonl` (ADR-0074 D6) — D2/D3 触发后重标注; D5 共存期间双版本

---

## 后续

### 短期 (Wave 2 Phase 2.4 Gating 检查)

1. 等待 ADR-0074 Evidence Gate D4 第一次测量结果 (parse-valid %)
2. 基于测量结果决定 D2 `$var` 是否触发
3. 收集 declarative style 用户反馈 (3 个月观察期)
4. 准备 D1 + D4 实施 (不依赖 gate)

### 中期 (Wave 2 Phase 2.4 实施, 估时 1-2 周)

5. **D1 强制实施**: `stream: true` 扩展到所有 tool_call / shell.exec / dsl_call
6. **D4 强制实施**: `backend:` 字段 (衔接 ADR-0075 D4)
7. DSL 规范 v3.10 → v3.11 升级
8. CI 测试覆盖: stream + backend + 强制字段

### D2 触发后 (条件性, 估时 2-3 周)

9. `$var` AST 节点 + 解析器
10. 双语法共存期启动 (6 月 countdown)
11. LLM Prompt V3.0 → V3.1 (标注 `$var` 优先)
12. 训练数据重标注 + V3.1 baseline 重测

### D3 触发后 (条件性, 估时 1 周)

13. `exec:` 语法糖解析器
14. LLM Prompt V3.5 (标注 declarative 优先)
15. 训练数据混用样本标注

### D6 触发后 (gated, 估时 3-4 周)

16. 子图表达力不足证明 (数据采集 + 分析)
17. `try/catch/finally` AST + executor + lint
18. 50+ error recovery examples 采集
19. 训练数据 V4.0 (error recovery 强化)

---

## 复审节点

- **2026-09-02 翻牌记录**: ADR-0072 状态从 🔍 Proposed → 🟡 Partial (D3 declarative style + D5 双语法共存期基础设施已 ship; D1+D4 待实施; D2 触发条件 Evidence Gate = Conditional 未达; D6 OFF; 治理异常"实施先于翻牌"已文档化)
- **D1 `stream:` 必补完成时**: ADR-0072 状态保持 🟡 Partial
- **D4 `backend:` 必补完成时**: ADR-0072 状态保持 🟡 Partial (D1+D4 全 ship 仅达到 §3.A 必补门槛)
- **D2 `$var` 触发时** (Evidence Gate 真实 3 模型重测 FAIL 即 parse-valid < 85%): 本 ADR 状态保持 🟡 Partial; 新增 §后续 Phase 2.5 (D2 实施 + D5 6 个月双语法共存期启动)
- **D1+D4+D2+D3+D5 全 ship + D6 维持 OFF** 时: ADR-0072 状态从 🟡 Partial → ✅ Approved
- **DSL v3.11 ship 时**: spec 同步升级; Wave 3 Evidence Gate 复测

---

*文档版本: v1.1*
*创建日期: 2026-08-03*
*作者: HydraForge 架构组*
*状态: 🟡 Partial (2026-09-02 翻牌 — D3+D5 已 ship per `from-roadmap-phase-6c-execution-dsl`, D1+D4 待实施, D2 因 Evidence Gate = Conditional 未触发维持 N/A, D6 per ADR-0071 §3.C 维持 OFF; 治理异常"实施先于翻牌"已文档化于头部状态行)*