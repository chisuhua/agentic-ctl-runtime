# fix-yield-node-token-passthrough

**Status**: Draft (scaffold)

## Scope

Wave 1 #1 commit `5dc4569` (fix-generator-request-model-default) 修复 YieldNode
的 model 遮蔽时, 明确记录了 `token={}` GAP (`node_executor.cpp:588` 注释) —
本 change 修此 GAP, 让流式 generate 可被外部 stop_token 取消.

1 改动: `src/modules/executor/node_executor.cpp:592`
- 原: `auto stream = llm_provider_->generate_stream(req, std::stop_token{});`
- 改: `auto stream = llm_provider_->generate_stream(req, token);`

1 测试: `tests/test_node_executor.cpp` 新增 (或既有 test 追加)
- YieldNode NEXT 模式 → 流式 generate → cancel stop_token → 流立即返回
- 验证 cancellation 端到端路径

## Why

`YieldNode` execute 路径 (line 459 execute_y  `std::stop_token token` 形参) 已正确
接收外部传入的 stop_token, 但 line 592 传给 `generate_stream` 时**硬编码 `std::stop_token{}`** —
外部 cancel 信号到 YieldNode 后丢失, 无法传播到流式 LLM 调用. 真实 LLM hang
时 (deepseek server 无响应), 父进程无法通过 cancel 解除 YieldNode 流式等待,
子进程永久 block.

Wave 1 #1 (commit 5dc4569) 修复 model 遮蔽时明确标注此 GAP 为独立 follow-up:
```cpp
// 注: token={} 属 fix-yield-node-token-passthrough scope (独立 follow-up),
// 本 commit 仅修 model 契约, 不触碰 token 透传.
```

## 验证

- `openspec validate fix-yield-node-token-passthrough --strict` exit 0
- `ctest -j$(nproc)` baseline 232 → 233 +1 new, 0 regression
- skip 模式 SUCCEED; 真实 deepseek 验证 token cancel 真触发流返回

## 依赖

- **依赖**: Wave 1 #1 fix-generation-request-model-default 已 ship（YieldNode 模型契约修复）
- **依赖**: Wave 1 #2 fix-cloud-adapter-multithreading 已 ship（流式走 SerializingDecorator 串行化）
- **被依赖**: Wave 3 Phase F (YieldNode multi-thread) 启用真实 LLM 测试

## Artifacts

- `proposal.md` — Why / Scope (1 改动 + 1 测试) / Impact / 依赖
- `design.md` — token 透传契约 + Wave 1 #1 GAP 记录 + 测试设计
- `tasks.md` — 10 sub-tasks across 3 phases
- `specs/fix-yield-node-token-passthrough/spec.md` — 2 ADDED Requirements
- `README.md` — status / scope / 依赖 / follow-up