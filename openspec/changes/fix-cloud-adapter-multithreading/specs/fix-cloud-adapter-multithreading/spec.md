# Spec: fix-cloud-adapter-multithreading

## Purpose

修复 `real-llm-core-coverage` Phase B 发现的 CloudLLMAdapter 多线程 SIGSEGV:
N≥2 worker 并发 + Authorization header + https 三者同时出现时
`httplib::Client::Post` 内部 `create_client_socket` 栈 corruption。

修复路径: 工厂层为 cloud adapter 注入 `SerializingDecorator`, 用 `std::mutex` +
`std::condition_variable` 串行化 generate / generate_stream 调用, 根除并发 race
(以牺牲并发性为代价, 但保留 worker 池的非 LLM 工作并发价值)。

## ADDED Requirements

### Requirement: CloudLLMAdapter SHALL serialize concurrent generate calls

`LLMProviderFactory::create` 为 cloud provider 路径 SHALL 自动注入 `SerializingDecorator`, 串行化 `generate` / `generate_stream` 调用, 根除多线程 SIGSEGV. 装饰 SHALL 对调用方透明 (返回类型仍为 `unique_ptr<ILLMProvider>`).

#### Scenario: LLMProviderFactory wraps CloudLLMAdapter with SerializingDecorator
- GIVEN `LLMConfig cfg { provider="deepseek", api_key="test", ... }`
- WHEN `LLMProviderFactory::create(cfg)` 调用
- THEN 返回的 provider 类型链是 `SerializingDecorator → CloudLLMAdapter`
- AND 外部调用方无感知 (透明)
- AND mock / llama provider 路径不包装 (性能不受影响)

#### Scenario: SerializingDecorator serializes N concurrent generate calls
- GIVEN 2 个 worker 线程同时调 `provider->generate(req, token)`
- WHEN 两个调用几乎同时进入
- THEN 第二个调用的 inner_ 调用**严格在第一个返回后**
- AND `total_serialize_waiters_` 反映等待者数
- AND `concurrent_count_` 在 inner 调用期间为 1, 其他时候为 0

#### Scenario: SerializingDecorator honors stop_token during wait
- GIVEN worker A 持有 inner_ (长 generate)
- AND worker B 在 cv_ 上等待
- WHEN worker B 的 stop_token.request_stop() 被调用
- THEN worker B 立即返回 `Result::failure(LLMError{Cancelled, ...})`
- AND inner_ 锁被释放 (worker A 可继续)
- AND 后续 waiter 可进入 inner_

#### Scenario: SerializingDecorator generate_stream also serialized
- GIVEN 4 worker 同时调 `provider->generate_stream(req, token)`
- THEN inner_->generate_stream 调用严格串行
- AND token 取消语义与 generate 一致

### Requirement: 真实 LLM 多线程压力 SHALL NOT SIGSEGV

`DomainWorkerPool` 多 worker 共享真实 deepseek provider SHALL 不触发 SIGSEGV / abort. 串行化后总耗时 SHALL 在合理范围内 (N × 单调用延迟 × 容忍系数).

#### Scenario: DomainWorkerPool 8 workers x 20 real deepseek tasks zero SIGSEGV
- GIVEN `DomainWorkerPool(8, bus)` + 共享 `provider` (经 SerializingDecorator)
- WHEN 提交 160 个真实 deepseek task
- THEN 160 全部 completed (handler 不抛, 全部返回)
- AND 零 SIGSEGV / zero abort
- AND 总耗时 < 5 分钟 (串行化后 ~160 × 3s/call = 8 min 上限, 实际 ~5min)
- AND Stress test 自动验证 (本地有 key + CI skip)

### Requirement: 解锁 real-llm-core-coverage Phase B B.2

本 change ship 后 SHALL 解锁 `real-llm-core-coverage` Phase B B.2 (N=4 worker 共享真实 deepseek provider), 移除测试中的 `WARN + SUCCEED` 占位, 恢复真实执行路径.

#### Scenario: B.2 启用 (移除 WARN + SUCCEED 占位)
- GIVEN fix-cloud-adapter-multithreading 已 ship
- AND `tests/test_domain_worker_pool.cpp` B.2 当前用 `WARN + SUCCEED` 占位
- WHEN `real-llm-core-coverage` 升级 (或本 change 包含此修改)
- THEN B.2 真实执行 `DomainWorkerPool(4) + shared real deepseek provider`
- AND 4 个 completed 全部 ok
- AND 无 SIGSEGV (production 修复保证)

### Requirement: scope 边界 (Out of Scope)

本 change SHALL NOT 触碰以下范围, 各自归属独立 change.

#### Scenario: 真根因修复 (OpenSSL + httplib) SHALL NOT be in this change
- 归属: 升级 OpenSSL 3.0 + httplib 最新版 + 可能 patch
- 理由: 排查时间 1-3 天, 阻塞 Phase B/E/G ship
- 本 change 仅用 SerializingDecorator 规避, 不查根因
- follow-up: ADR-XXXX "Cloud adapter threading model" 升级到方案 B

#### Scenario: Token passthrough fixes SHALL NOT be in this change
- 归属: `fix-yield-node-token-passthrough` / `fix-orchestrator-token-passthrough` /
  `fix-skill-interpreter-token-and-timeout`
- 理由: 不同问题域 (cancel propagation)

#### Scenario: Mock provider SHALL NOT be wrapped by SerializingDecorator
- 理由: mock 无 SIGSEGV, 串行化降低 Phase B B.3 性能 (100 串行 → 更慢)
- AND mock 路径在 test / 调试时常用, 性能不敏感反破坏开发体验

## 验证标准

- `cmake --build build -j$(nproc)` 编译通过 (0 error, 0 warning)
- `LLMProviderFactory::create("deepseek")` 返回 SerializingDecorator 包装 (unit + integration)
- 4 个 unit test PASS (forward / serialize / stop_token / available_models)
- 1 个 integration test PASS (factory 路径)
- 1 个 stress test 真实 deepseek 8 worker × 20 tasks 零 SIGSEGV (本地有 key)
- `real-llm-core-coverage` Phase B B.2 启用并 PASS
- baseline 224 → 230+ new PASS, 0 regression
- `openspec validate fix-cloud-adapter-multithreading --strict` exit 0
- `tools/adr_lint.py` 0 errors
- `tools/docs_drift_audit.py` 0 CRITICAL drift

## 性能 trade-off

| 场景 | 修改前 | 修改后 |
|---|---|---|
| 单 worker 真实 deepseek | t | t (无变化) |
| 4 worker 并发真实 deepseek | 4× 并行 (期望 t) | 4× 串行 (实际 4t, 但 SIGSEGV → 不可用) |
| 4 worker 并发 mock provider | 4× 并行 (t) | 4× 并行 (t, 不包装) |
| 4 worker handler 跑非 LLM 工作 + 偶发 LLM | 4× 并发 (t) | 4× 并发非 LLM + LLM 串行 (~t + n*t_llm) |

**结论**: 牺牲并发 LLM 调用换取零 SIGSEGV, 对 DomainWorkerPool 整体价值
(worker 仍并发跑非 LLM 工作) 影响有限. 真根因修复 (方案 B) 留 ADR-XXXX.

## References

- **根因诊断**: `real-llm-core-coverage` Phase B ship (commit `c0cb522`) +
  Oracle 审查 + gdb backtrace
- **诊断排除**: 单线程 + Authorization PASS / 单线程真实 deepseek PASS /
  4 worker 并发 mock provider PASS
- **设计依据**: httplib + OpenSSL 多线程约束
- **关联修复**: `fix-generation-request-model-default` (model 遮蔽, 不同问题域)
