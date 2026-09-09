# Spec: fix-yield-node-token-passthrough

## Purpose

修复 Wave 1 #1 commit `5dc4569` (fix-generator-request-model-default) 标注的 `token={}` GAP:
`src/modules/executor/node_executor.cpp:592` 硬编码 `std::stop_token{}` 应替换为
外部传入的 `token` 形参. 修复后 YieldNode 流式 generate_stream 可被外部 stop_token
取消 (真实 LLM hang 时父进程可中断).

## ADDED Requirements

### Requirement: YieldNode execute_y SHALL pass stop_token to generate_stream

`YieldNode::execute_y` SHALL 将外部传入的 `std::stop_token token` 形参 (line 459) 透传
至 `llm_provider_->generate_stream(req, token)` (line 592), 替换原硬编码 `std::stop_token{}`.
外部 cancel 信号 SHALL 经 YieldNode 传播至流式 provider, 触发流立即返回.

#### Scenario: YieldNode stream cancellation via stop_token passthrough
- GIVEN YieldNode NEXT 模式 + std::stop_source
- AND MockLLMProvider::generate_stream 支持 token-aware (cancel 时返回 false/部分 chunk)
- WHEN std::jthread 启动 `executor.execute_yield(&node, ctx, ss.get_token())`
- AND 流进入 (mock 返回第 1 chunk)
- AND `ss.request_stop()` 触发
- THEN worker.join() 在合理时间内返回 (≤ 1s, 远小于 LLM 调用延迟)
- AND `generate_stream` 收到 cancel 信号 (mock 返回 false / 流结束)
- AND 不死锁 (外部 cancel 传播至 generate_stream 成功)

### Requirement: scope 边界 (Out of Scope)

本 change SHALL NOT 修改 YieldNode Node 结构体或 execute_y 签名; 下列 SHALL 明确排除.

#### Scenario: YieldNode Node 结构体 SHALL NOT be changed
- 理由: YieldNode 已 ship (Sprint 18+), 字段稳定; 仅修复 execute_y 内部 token 透传
- AND Phase F 启用真实 LLM 时, 结构体仍适用 (yield_mode NEXT/CONTINUE)

#### Scenario: execute_y 签名 SHALL NOT be changed
- 理由: execute_y(line 459) 签名已正确 `std::stop_token token`, 只需 line 592 内部替换
- AND 形参 token 来源 (caller via DSLEngine) 不变

#### Scenario: Multi-thread YieldNode 流式真实 LLM SHALL NOT be in this change
- 归属: Wave 3 Phase F (YieldNode multi-thread) 独立 change
- 理由: 本 change 仅 1 行 token 透传修复, 多 worker 并发真实 LLM 测试需 DomainWorkerPool 集成

#### Scenario: CloudLLMAdapter server-side cancel SHALL NOT be in this change
- 归属: ADR-0087 cloud-adapter-threading-model root cause fix (Wave 4, 5-7 天升级)
- 理由: server-side cancel 需 OpenSSL 3.0 + httplib 升级, 不属本 change scope

## 验证标准

- `cmake --build build -j$(nproc)` 编译通过 (0 error, 0 warning)
- 1 new test case PASS (YieldNode stream cancellation via stop_token passthrough)
- skip 模式: 1 case SUCCEED short-circuit (CI 友好)
- 既有 mock tests 零回归 (YieldNode NEXT/CONTINUE/Done/Retry cases)
- 全量 `ctest -j$(nproc)` baseline 232 → 233 +1 PASS, 0 regression
- `openspec validate fix-yield-node-token-passthrough --strict` exit 0
- `tools/adr_lint.py` 0 errors
- `tools/docs_drift_audit.py` 0 CRITICAL drift

## 依赖

| 上游 | 状态 | 影响 |
|---|---|---|
| Wave 1 #1 fix-generation-request-model-default | ✅ SHIPPED | YieldNode 模型契约修复 (req.params.model.clear()), token 透传 GAP 同时标注 |
| Wave 1 #2 fix-cloud-adapter-multithreading | ✅ SHIPPED | 流式 generate_stream 走 SerializingDecorator, token 经其透传至 inner_->generate_stream |

| 下游 (unblocked) | 内容 |
|---|---|
| Wave 3 Phase F YieldNode multi-thread | 本 fix unblock Phase F 真实 LLM 流式测试 (F.1-F.3) |

## References

- **GAP 位置**: `src/modules/executor/node_executor.cpp:592`
- **GAP 标注**: Wave 1 #1 commit `5dc4569` message + line 588 注释 ("token={} 属 fix-yield-node-token-passthrough scope")
- **测试模式**: Phase D cost-tracking-decorator-realllm (mock + skip short-circuit)
- **追溯 Sprint**: Sprint 18 YieldNode 实施 + Wave 1 #1/#2 real-llm 基础设施