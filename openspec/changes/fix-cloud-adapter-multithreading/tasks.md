## Phase 1 — 代码（2 改动）

- [ ] 1.1 新建 `include/agenticdsl/contract/serializing_decorator.h`
      — 接口: `SerializingDecorator : public ILLMProviderDecorator`
      — 成员: inner_, mutex_, cv_, concurrent_count_, total_serialize_waiters_, purpose_
- [ ] 1.2 新建 `src/common/llm/serializing_decorator.cpp`
      — generate() / generate_stream() 实现 (设计见 design.md §修复方案)
      — `cv_.wait` 替代 `lock_guard` 支持 stop_token 取消等待
- [ ] 1.3 修改 `src/common/llm/llm_provider_factory.cpp` `create(LLMConfig)`
      — cloud 路径 (`deepseek/openai/anthropic/qwen/...`) 自动包 `SerializingDecorator`
      — mock / llama 路径不包装
- [ ] 1.4 修改 `tests/CMakeLists.txt` 添加 `test_serializing_decorator` target

## Phase 2 — 测试（4 unit + 1 integration + 1 stress + B.2 启用）

- [ ] 2.1 新建 `tests/test_serializing_decorator.cpp` 4 cases
      — forwards generate result (单 worker 透传)
      — serializes concurrent generate calls (2 worker 同时, 内层串行)
      — honors stop_token during wait (cv_.wait + token.stop_requested)
      — available_models delegates (透传)
- [ ] 2.2 新建 `tests/test_llm_provider_factory_decorator.cpp` 1 case
      — LLMProviderFactory::create("deepseek") 返回 SerializingDecorator 包装
      — require_real_llm_env + 短时生成验证 (1 worker, ~5s)
- [ ] 2.3 新建 `tests/test_cloud_adapter_multithread.cpp` 1 stress case
      — DomainWorkerPool(8) + 共享 provider + 160 tasks 真实 deepseek
      — 断言: 160 completed 全部 ok, 零 SIGSEGV, elapsed < 5 min (serialized)
      — `[realllm][stress][multithread]` tag, CI skip 默认 SUCCEED 占位
- [ ] 2.4 启用 `real-llm-core-coverage` Phase B B.2
      — 删除 `tests/test_domain_worker_pool.cpp` B.2 的 `WARN + SUCCEED + return`
      — 改回真实执行路径
      — 验证 ctest 全 PASS (本地有 key)

## Phase 3 — 验证 + archive

- [ ] 3.1 `cmake --build build -j$(nproc)` 编译通过 (0 error, 0 warning)
- [ ] 3.2 `ctest -j$(nproc)` baseline 224 → 230+ new (4 unit + 1 integration + 1 stress + B.2 启用) PASS, 0 regression
- [ ] 3.3 真实 deepseek stress 手动验证 (8 worker × 20 tasks) 零 SIGSEGV
- [ ] 3.4 `openspec validate fix-cloud-adapter-multithreading --strict` exit 0
- [ ] 3.5 `tools/adr_lint.py` 0 errors
- [ ] 3.6 `tools/docs_drift_audit.py` 0 CRITICAL drift
- [ ] 3.7 commit (Phase 1 代码 1 commit + Phase 2 测试 1 commit + Phase 3 验证 1 commit)
- [ ] 3.8 archive (`openspec archive fix-cloud-adapter-multithreading`)
- [ ] 3.9 follow-up note: 在 `real-llm-core-coverage` tasks.md §H.1 加 "ADR-XXXX Cloud adapter threading model" 待办

## Tasks 总数

| Phase | Sub-tasks | New test cases |
|---|---|---|
| 1 (代码) | 4 | — |
| 2 (测试) | 4 | 4 (unit) + 1 (integration) + 1 (stress) + 1 (B.2 re-enable) |
| 3 (验证) | 9 | — |
| **Total** | **17 sub-tasks** | **~7 new artifacts** |

## 估时

| Phase | 估时 |
|---|---|
| 1 | 1.5 h |
| 2 | 2 h (含真实 LLM stress 调试) |
| 3 | 30 min |
| **Total** | **~4 h** |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| `cv_.wait` + `concurrent_count_` 设计有 race | unit 测试 + 真实 stress 验证 |
| `SerializingDecorator` 包装让 mock 测试变慢 | factory 只包装 cloud, mock 路径不变 |
| B.2 启用后仍偶发 SIGSEGV (其他根因) | stress test 先于 B.2, 验证 stress PASS 后再启用 B.2 |
| OpenSSL/httplib 真根因仍未解决 (follow-up) | tasks.md §3.9 留 note, 避免遗忘 |
| `LLMProviderFactory::create` 路径修改影响既有测试 | 全量 ctest 跑通 (test_cloud_llm_live 等) |
