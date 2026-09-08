## Why

`real-llm-core-coverage` Phase B 实施时（commit `c0cb522`）发现了一个多线程
CloudLLMAdapter 的生产代码 SIGSEGV bug:

**触发条件**（已通过 gdb 实证 + 消除实验定位）:
- `N≥2` worker 并发调 `CloudLLMAdapter::do_post`
- 带 `Authorization: Bearer <api_key>` header（即 `config_.api_key` 非空）
- https URL（OpenSSL 路径）

**症状**:
- gdb backtrace: `do_post` → `httplib::Client::Post` → `ClientImpl::send_` →
  `create_client_socket` → 构造 `std::function<void(int)>` → 跳到空地址
  (`0x11`) → SIGSEGV
- `socket_options_` (ClientImpl 成员, `std::function<void(socket_t)>`) 栈 corruption
- 单线程 `pool(1)` + 真实 deepseek PASS (A.2 已 ship, B.2 pool(1) 11 assertions PASS)
- 无 Authorization 的 mock provider 路径 PASS (B.3 100 串行 / B.4 4 并发 PASS)
- `HttpLLMAdapter` (test_http_adapter) 单独模式 PASS

**诊断排除**:
- ❌ httplib 自身: `HttpLLMAdapter` 相同路径 PASS
- ❌ 单线程本身: `pool(1)` 真实 deepseek PASS
- ❌ 多线程 SSL 本身: `MockLLMProvider` 4 worker 并发 PASS
- ✅ 多线程 CloudLLMAdapter + Authorization + https 三者同时出现 → SIGSEGV

**根因候选**:
1. **OpenSSL `SSL_CTX` 多线程初始化竞态** — httplib 或 OpenSSL 默认 SSL_CTX
   不是 per-thread, 多线程同时首次建立 SSL 连接触发 `SSL_new`/`SSL_CTX_use_certificate`
   等内部状态 corruption. OpenSSL 1.1+ 自带线程安全, 但需要正确链接 + 锁初始化.
2. **httplib Authorization header 栈处理 bug** — httplib 内部对某些 header
   组合 + 并发存在栈使用问题 (回归已知? 需查 httplib issue tracker).

无论根因, **修复必须让多线程共享 provider 路径稳定**, 否则 Phase E (Skill IPC),
G (ContextCompactor), 以及 pdk_chat_demo 多 agent 场景都无法使用真实 LLM.

## What Changes

### Scope: production code 修复（最小心智爆炸半径）

**方案 (待验证优先级)**:

1. **方案 A — 工厂层串行化 generate()** (最小侵入):
   - 在 `LLMProviderFactory::create` 时为 `CloudLLMAdapter` 包一层
     `std::mutex` 串行化装饰器 (`SerializingDecorator`)
   - 影响: 牺牲并发性 (4 worker 实际串行), 但零 SIGSEGV, 零 API 变更
   - 优: 1 个新文件 + 1 行 factory 改动, 立即 ship 修复
   - 缺: 牺牲了 DomainWorkerPool 的并发价值 (但 worker 仍可并发跑非 LLM 工作)

2. **方案 B — OpenSSL 多线程 init + httplib 修复** (根因修复):
   - 在 LLMProviderFactory 构造时调 `SSL_library_init()` + `SSL_load_error_strings()` +
     `OpenSSL_add_all_algorithms()` (legacy) 或确认 OpenSSL 1.1+ 自带 init
   - 添加 `CRYPTO_set_id_callback` + `CRYPTO_set_locking_callback` (OpenSSL 1.0)
   - 检查 httplib 是否有已知 issue (若 version 升或 patch 可解决)
   - 影响: 真正支持并发, 修复根因
   - 优: 恢复真实并发价值
   - 缺: 排查时间长 (1-3 天), 根因可能涉及上游库

3. **方案 C — shared provider 改 per-task 克隆** (避免共享):
   - DomainWorkerPool handler 改为每次构造临时 provider (但 ILLMProvider
     无 clone 接口, 需 factory.clone() 或 factory.create() 重复构造)
   - 影响: CloudLLMAdapter 不共享 → 无 SIGSEGV
   - 优: 隔离并发隐患在 pool 层, adapter 不变
   - 缺: 重复构造开销 (每次 generate 都新建 provider) + 修改 pool API

**默认采用方案 A** (最小侵入, 1 天 ship) + 留 follow-up note 升级到方案 B.

## Scope Boundaries (In)

- ✅ CloudLLMAdapter 多线程 generate 串行化 (`SerializingDecorator`)
- ✅ 串行化包装通过 `LLMProviderFactory::create` 自动应用 (零调用方改动)
- ✅ Recording provider 单元测试验证包装应用 (`SerializingDecorator` record 实际并发调用)
- ✅ 多线程 stress test (4-8 worker 并发 N 次真实 deepseek) 验证零 SIGSEGV
- ✅ 解锁 Phase E (Skill IPC) / G (ContextCompactor) / B.2 真实 LLM 路径

## Scope Boundaries (Out)

- ❌ 不修改 httplib 上游库 (方案 B 的根因修复, 留 follow-up)
- ❌ 不重构 DomainWorkerPool handler signature (方案 C 涉及, 留 follow-up)
- ❌ 不修改 `LLMProviderFactory::create` 现有返回类型 (仅内部包装)
- ❌ 不为 mock/local provider 串行化 (mock 无 SIGSEGV, 不需)
- ❌ 不修 token passthrough (属独立 changes)

## Impact

**修复站点**: `src/common/llm/llm_provider_factory.cpp` (1 处装饰插入) +
新增 `src/common/llm/serializing_decorator.h/.cpp` (~80 行)

**性能影响**:
- 真实 LLM 路径: N 个 worker 共享 provider → 串行 generate (一时刻只有 1 个
  在 HTTP). 延迟累加: 4 worker × 5s/调用 = 20s (vs 并发 5s).
- 对 DomainWorkerPool 价值: 4 worker 仍可并发跑 handler 中的非 LLM 工作
  (例如序列化/查找), 仅 LLM 调用部分串行 — 实际提升仍显著.
- mock 路径: 无影响 (不包装).

**新增测试**:
- 单元: `SerializingDecorator` forward + 串行化行为 (2-3 cases)
- 集成: `LLMProviderFactory::create("deepseek")` 返回的 provider 经多线程
  stress (8 worker × 20 tasks 真实 deepseek) → 零 SIGSEGV, 全部成功

**ctest 影响**: +3 unit + +1 stress (real LLM, `[realllm][stress]` tag, CI skip 默认)

**CI 影响**: 新 stress test 在 `HYDRAFORGE_SKIP_REAL_LLM=1` 模式下 SKIP (CI 默认
绿色), 真实 LLM 验证走手动 `workflow_dispatch` 或本地有 key 环境

**Non-goals**:
- ❌ 不实现真正的并发 (留给方案 B)
- ❌ 不改 httplib/OpenSSL 升级路径 (独立技术债)

## 验证标准

- `cmake --build build -j$(nproc)` 编译通过 (0 error, 0 warning)
- `LLMProviderFactory::create("deepseek")` 返回的 provider 实例在 8 worker × 20
  tasks 真实 deepseek 压力下零 SIGSEGV
- `real-llm-core-coverage` Phase B B.2 SKIP'd 测试启用 (移除 SKIP, 改为真实运行)
- baseline 224 ctest + 新 unit + stress 全部 PASS
- `openspec validate fix-cloud-adapter-multithreading --strict` exit 0
- `tools/adr_lint.py` 0 errors
- `tools/docs_drift_audit.py` 0 CRITICAL drift

## 估时

| 阶段 | 内容 | 估时 |
|---|---|---|
| 1 | `SerializingDecorator` 实现 + factory 集成 | 1.5 h |
| 2 | 单元测试 (decorator 行为) | 45 min |
| 3 | 集成 stress test (多线程 + 真实 deepseek) | 1 h (调试) |
| 4 | B.2 启用 (移除 SKIP, 改 SUCCEED) | 30 min |
| 5 | 验证 + commit + archive | 20 min |
| **Total** | | **~4 h** |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| 串行化导致性能下降使某些场景不适用 | tasks.md §3 记录性能 trade-off; follow-up note 升级方案 B |
| 串行化 `std::mutex` 自身在某些边缘情况下仍有 race | unit 测试 + stress test 验证 |
| LLMProviderFactory::create 修改影响现有测试 | 全量 ctest 跑通; 既有 test_cloud_llm_live 等已知 PASS 测试不依赖并发 |
| OpenSSL 库版本差异导致 init 失败 (follow-up 方案 B) | 本 change 不触碰 OpenSSL, 风险隔离 |
| mock provider 也被串行化导致现有测试变慢 | factory 只包装 cloud (deepseek/openai/anthropic 等), 不包装 mock |
