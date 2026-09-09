## Why

Wave 1 #1 commit `5dc4569` 修复 YieldNode 模型遮蔽时, 明确标注 `node_executor.cpp:588`
(token={}) GAP 为独立 follow-up:
```cpp
// 注: token={} 属 fix-yield-node-token-passthrough scope (独立 follow-up),
// 本 commit 仅修 model 契约, 不触碰 token 透传.
auto stream = llm_provider_->generate_stream(req, std::stop_token{});
```

**GAP 现象**: `YieldNode::execute_y` (node_executor.cpp:459) 正确接收外部传入的
`std::stop_token token` 形参, 但 line 592 传给 `llm_provider_->generate_stream` 时
**硬编码 `std::stop_token{}`** — 外部 cancel 信号到 YieldNode 后丢失, 无法传播到
流式 LLM 调用.

**实际影响**: 真实 LLM (deepseek) hang 时 (server 无响应 / 网络阻塞), 父进程
无法通过 `std::stop_source` 取消 YieldNode 流式等待. 测试场景: 测试或父进程
设 5s 超时 → cancel → YieldNode 不响应 → 父进程永久 block 至 LLM server 响应.

## What Changes

### Scope: 1 改动 + 1 测试

1. **生产代码 1 行**: `src/modules/executor/node_executor.cpp:592`
   - 原: `auto stream = llm_provider_->generate_stream(req, std::stop_token{});`
   - 改: `auto stream = llm_provider_->generate_stream(req, token);`

2. **测试 1 case**: `tests/test_node_executor.cpp` 新增 (或 test_yield_node.cpp 独立)
   - 构造 YieldNode NEXT 模式 + 真实/Mock LLM provider
   - 启动 execute_y 在 std::jthread
   - 短暂等待流进入 → `stop_source.request_stop()` → 验证流立即返回
   - 不引入新 mock 基础设施（复用既有 MockLLMProvider + 测试基类）

### 关键技术约束

**A. 不修改 execute_y 签名**: 形参 `std::stop_token token` 已正确, 只需 line 592 替换
**B. 不引入额外依赖**: 1 行 token 透传 + 1 测试
**C. 与 Wave 1 #2 fix-cloud-adapter-multithreading 协同**:
- 流式 generate_stream 走 SerializingDecorator (mutex + cv 串行化)
- 串行化不影响 token 透传 (token 透传到 inner_->generate_stream)
- 验证 token 取消在 SerializingDecorator 同步段生效

### Scope Boundaries (In)

- ✅ `src/modules/executor/node_executor.cpp:592` 1 行 token 替换
- ✅ `tests/test_node_executor.cpp` 或 `tests/test_yield_node.cpp` 新增 1 case
- ✅ 既有 YieldNode mock tests 零回归
- ✅ 更新 Wave 1 #1 注释 (line 588 删除 "token={} 属 fix-yield-node-token-passthrough" 注)

### Scope Boundaries (Out)

- ❌ 不修改 YieldNode Node 结构体 (`src/core/types/node.h`)
- ❌ 不修改 execute_y 签名 (已正确)
- ❌ 不实现 YieldNode multi-thread 真实 LLM 测试 (Wave 3 Phase F scope, 后续)
- ❌ 不实现 BudgetChecker 流式 token 取消联动 (V2 deferred)

## Impact

**总计 ctest 影响**: +1 cases, baseline 232 → 233 (无 regression)
**CI 影响**: 全部 `[realllm]` tag (若用真实 LLM) 或 `[yield_node][token]` (若用 mock), skip 模式 SUCCEED
**风险**: 极低 (1 行 token 透传, 与 execute_y 形参一致)

**Non-goals**:
- ❌ 不验证 SerializingDecorator 取消语义 (Wave 1 #2 ship Oracle APPROVE 已覆盖)
- ❌ 不验证 BudgetChecker 与 token 取消的交互 (独立 V2 deferred)

## 升级触发 (Escalation)

若实施时发现:
- **Wave 3 Phase F 真实 LLM 流式测试需要更深层 token 支持** → 扩 scope (本 change 仅 1 行)
- **测试 mock provider 不支持 token 取消** → 需 MockLLMProvider 增强 (Phase F 或独立)

## 验证标准

- `cmake --build build -j$(nproc)` 编译通过 (0 error, 0 warning)
- `tests/test_node_executor.cpp` (或 `test_yield_node.cpp`) 1 new case:
  - YieldNode NEXT 模式 + std::stop_source cancel → 流立即返回 — PASS
- skip 模式: 1 case SUCCEED short-circuit
- 既有 mock tests 零回归
- 全量 `ctest -j$(nproc)` baseline 232 → 233 +1 PASS, 0 regression
- `openspec validate fix-yield-node-token-passthrough --strict` exit 0
- `tools/adr_lint.py` 0 errors
- `tools/docs_drift_audit.py` 0 CRITICAL drift

## 估时

| 阶段 | 内容 | 估时 |
|---|---|---|
| 1 | 读 node_executor.cpp:459-610 + 既有 test + 写 1 行 + 1 case | 30 min |
| 2 | ctest + openspec validate + archive | 15 min |
| **Total** | | **~45 min** |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| MockLLMProvider generate_stream 不支持 token 取消 | 检查现有实现; 若无, 加 30 行 hook (token-aware sleep, 见 Phase D 经验) |
| 真实 LLM 取消需 deepseek server 支持 (server-side cancel) | skip 模式 SUCCEED; mock 测试 token 透传即可验证 path |
| node_executor.cpp:592 token 替换后, 真实 LLM hang 时仍可能 hang | 这是 SerializingDecorator/CloudLLMAdapter 自身 cancel 语义 (Wave 1 #2 ship 已覆盖); 本 change 仅验证 token 透传到 llm_provider_->generate_stream |
| Phase F 启用后, stream 取消与 BudgetChecker 冲突 | V2 deferred (见 scope out) |