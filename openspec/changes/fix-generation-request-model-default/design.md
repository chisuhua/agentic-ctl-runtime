# Design — fix-generation-request-model-default

## 修复方案

每个潜伏站点加 `req.params.model.clear()`（或确保构造 `GenerationRequest` 后
`params.model` 为空）。CloudLLMAdapter::build_request_body L164
`req.params.model.empty() ? config_.model : req.params.model` 自然 fallback 到
adapter 构造时 `LLMProviderFactory.create(cfg)` 设置的 `cfg.model`（如
`deepseek-v4-flash`）。

### 修复模板

```cpp
// 修复前（node_executor.cpp:356 等 5 站点）
GenerationRequest req;
req.prompt = ...;
auto result = llm_->generate(req, token);  // req.params.model = "gpt-4o-mini" (默认遮蔽)

// 修复后
GenerationRequest req;
req.prompt = ...;
// LLMParams = LLMConfig 别名, 默认 model = "gpt-4o-mini" (非空);
// 若不清空, CloudLLMAdapter::build_request_body L164 (req.params.model.empty() ?
// config_.model : req.params.model) 会拿默认 "gpt-4o-mini" 遮蔽 provider 配置的
// 真实 model (如 deepseek-v4-flash) → server 拒绝 ("you passed gpt-4o-mini").
// 站点无 model 概念 (LLMConfig 由 adapter/factory 持有), 清空让 adapter fallback.
// 实测: real-llm-core-coverage Phase A A.2 测试中, 加此行后 deepseek 真实调用 PASS.
req.params.model.clear();
auto result = llm_->generate(req, token);
```

### 5 站点具体修复位置

1. **`node_executor.cpp:356`** (GenerateSubgraphNode `execute`):
   - 修复点: `req.params.model.clear();` 加在 `req.prompt = ...` 之后, `llm_->generate(req, ...)` 之前
   - 上下文: GenerateSubgraphNode 调用 LLM 生成子图 DSL
2. **`node_executor.cpp:574`** (YieldNode `execute`):
   - 修复点: `req.params.model.clear();` 加在 YieldNode 构造 `GenerationRequest req` 后
   - 上下文: YieldNode 流式调用 `llm_->generate_stream(req, {})` 前的 req
3. **`skill_interpreter.cpp:657-659`** (IPC `llm_generate`):
   - 修复点: `gen_req.params.model.clear();` 加在 `gen_req.prompt = prompt;` 后
   - 上下文: skill_child_main 子进程调 IPC handler `llm_generate("...")`, 父进程 LLM 调用
4. **`src/core/context_compactor.cpp:60`** (ContextCompactor `compact`):
   - 修复点: `req.params.model.clear();` 加在 req prompt 设置后
   - 上下文: ContextCompactor 摘要 LLM 调用
5. **`src/modules/cognitive/gepa_loop.cpp:115`** (GEPA loop reflection):
   - 修复点: `request.params.model.clear();` 加在 `GenerationRequest request("Reflect on failed execution " + ...)` 构造后
   - 上下文: GEPA 反射 LLM 生成 skill 编译

## 测试设计

### 单元测试模式 (recording provider, Oracle P1-1 风格)

每个站点 1 个 recording provider 单测, 验证该站点的 handler/函数传给
`generate()` 或 `generate_stream()` 的 req.params.model 为空:

```cpp
// 示例 (context_compactor)
class RecordingLLMProvider : public ILLMProvider { ... };

TEST_CASE("ContextCompactor passes empty model to provider") {
  auto recorder = std::make_unique<RecordingLLMProvider>();
  recorder->result.text = "summary";
  // ... 构造 ContextCompactor, 注入 recorder ...
  // 调用 compact(ctx)
  REQUIRE(recorder->generate_calls == 1);
  REQUIRE(recorder->last_model.empty());
}
```

### 集成验证

1 个跨 5 站点断言 (用 grep 或编译期检查):
- 每个站点的 `GenerationRequest req/request/gen_req` 构造后必须出现
  `req.params.model.clear()` 或等效 (不设 model)
- 推荐: 静态契约脚本 `scripts/check-model-default-cleared.sh` (grep `req.params.model.clear()` 在 5 站点附近) — 防未来误删

### CI 友好性

所有新测试**不依赖真实 API key**:
- Recording provider 返回固定 JSON
- MockLLMProvider 覆盖 cloud adapter 调用
- CI skip=1 模式 + 无 key 时仍 PASS

## Telemetry 副作用 (已文档化, Oracle ship-with-fixes P2 修正)

Decorator 链记录/发射 model 标签:
- `cost_tracking_decorator.cpp:39` `budget_->record_llm_call(total_tokens, req.params.model)` → record "" (此前错误但非空的 "gpt-4o-mini")
- `compliance_decorator.cpp:65` `model = req.params.model` → audit 记录 ""
- `tracing_decorator.cpp:51,57` event payload `{"model", req.params.model}` → 事件 model ""

**影响范围** (Oracle ship-with-fixes session `ses_f7e28d67affeOFrht9yY4IJRxn` 修正后表述):
- ✅ **不影响**: token 计数 (Cost 仍按 prompt+completion tokens 计费, 数不变)
- ⚠️ **影响**: 预算**费率** (per-token rate model 相关):
  - `budget_controller.cpp:23 cost_per_token_for()`: `"gpt-4o-mini"` → 0.00000015, `""` → 通用 fallback 0.000001
  - **方向**: 保守方向, 旧值 `"gpt-4o-mini"` 本就对非 OpenAI 端点不准; 新通用费率 6.7× 但接近实际
  - **Phase D 决策点**: 是否在 decorator 层加 `effective_model(req)` 访问器让 telemetry label 与 L164 fallback 一致 (model 标签保真属正交问题, 不在本 change 范围)
- ✅ **不影响**: 事件可达性 / bus dispatch
- ⚠️ **影响**: cost/event 报告中 model 字段为空 (cosmetic)
- ⚠️ **影响**: 任何 mock/单测断言 `meta["model"] == "deepseek-v4-flash"` 失败 — 需在 Phase D / Phase C 适配

**不要**用 `= "unknown"` 替代 `clear()`——非空值直接破坏 L164 fallback (server 收到 "unknown" → 拒绝),
`clear()` 是唯一正确机制.

**Phase D (Cost) 前 note**: `real-llm-core-coverage/tasks.md` D 阶段加 "assertion 容忍 model="" 或显式设值" 提醒.

## 兼容性

**BREAKING**: 对显式断言 `req.params.model == "gpt-4o-mini"` 的测试会 FAIL —
但本仓库无此类测试 (grep 已验证). 无现有用户面 API 变化.

**Adapter 层**: CloudLLMAdapter L164 行为不变. 仅调用方传值变化.

**Mock 路径**: `LLMParams.model = ""` 时 mock provider 不解析 model 字段 (mock
直接返回 fixed_response), 零影响.

## 实施顺序

1. **Phase 1 (代码)**: 5 站点按"出现频率"顺序修复:
   - simple_orchestrator.cpp (already fixed by real-llm-core-coverage)
   - node_executor.cpp:356 + :574 (同一文件, 1 commit)
   - skill_interpreter.cpp:657-659 (独立 commit)
   - context_compactor.cpp:60 (独立 commit)
   - gepa_loop.cpp:115 (独立 commit)
   - 共 4 commits (orchestrator 已 ship, 不重做)

2. **Phase 2 (测试)**: 5 个 recording provider 单元 + 1 集成脚本

3. **Phase 3 (验证)**: ctest + openspec validate + adr_lint

每 commit 独立 (atomic commit 原则, 见根 AGENTS.md §治理层 模式 4).
