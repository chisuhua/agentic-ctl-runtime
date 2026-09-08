# Design — fix-cloud-adapter-multithreading

## 修复方案 (默认 A: 工厂层串行化装饰器)

### 架构

新增 `SerializingDecorator` (类比 `RateLimitDecorator` / `ComplianceDecorator` /
`TracingDecorator`):

```
User → LLMProviderFactory.create("deepseek")
        ↓
       CloudLLMAdapter (config.model = "deepseek-v4-flash", api_key set)
        ↑ wrapped by
       SerializingDecorator (新增)
        - 持有 std::mutex
        - generate() / generate_stream() 在锁内调 inner_
```

```cpp
// include/agenticdsl/contract/serializing_decorator.h (新增)
class SerializingDecorator : public ILLMProviderDecorator {
 public:
  SerializingDecorator(std::unique_ptr<ILLMProvider> inner,
                        std::string purpose = "default");
  Result<GenerationResult, LLMError> generate(
      const GenerationRequest& req, std::stop_token token) override;
  std::unique_ptr<IGenerationStream> generate_stream(
      const GenerationRequest& req, std::stop_token token) override;
  std::vector<ModelInfo> available_models() const override;
 private:
  std::unique_ptr<ILLMProvider> inner_;
  std::string purpose_;
  std::mutex mutex_;
  std::condition_variable cv_;
  int concurrent_count_ = 0;
  std::atomic<std::size_t> total_serialize_waiters_{0};
};
```

`generate()` 实现:
```cpp
Result<GenerationResult, LLMError> SerializingDecorator::generate(
    const GenerationRequest& req, std::stop_token token) {
  std::unique_lock<std::mutex> lock(mutex_);
  total_serialize_waiters_.fetch_add(1);
  cv_.wait(lock, [this, &token] {
    return concurrent_count_ == 0 || token.stop_requested();
  });
  if (token.stop_requested()) {
    total_serialize_waiters_.fetch_sub(1);
    cv_.notify_one();
    return Result::failure(LLMError{Cancelled, "Cancelled while waiting for serializer"});
  }
  ++concurrent_count_;
  total_serialize_waiters_.fetch_sub(1);
  lock.unlock();  // 释放 mutex 允许其他 waiter 检查 cv (但仍由 cv 保证互斥)

  // 实际 generate — 此时其他线程在 cv 上等待
  auto result = inner_->generate(req, token);

  lock.lock();
  --concurrent_count_;
  cv_.notify_all();  // 唤醒下一个 waiter
  return result;
}
```

**关键设计**:
- `concurrent_count_` 而非简单 mutex: 允许将来升级 (e.g. `max_concurrent=2`)
  而无需重写 API
- `total_serialize_waiters_` 用于诊断 + 未来测试断言 (验证确实串行化)
- `cv_.wait` 而非 `lock_guard`: 支持 `stop_token` 取消等待 (避免死锁)
- `generate_stream` 同样串行化

### Factory 集成 (`llm_provider_factory.cpp`)

```cpp
// 修改 LLMProviderFactory::create
std::unique_ptr<ILLMProvider> LLMProviderFactory::create(const LLMConfig& config) {
  // ... 既有 mock/cloud/llama 分发 ...
  if (backend == "deepseek" || backend == "openai" || ...) {
    auto adapter = std::make_unique<CloudLLMAdapter>(config);
    // 包装 SerializingDecorator 修复多线程 SIGSEGV
    return std::make_unique<SerializingDecorator>(
        std::move(adapter), "cloud-" + config.provider);
  }
  // mock / llama 不包装
}
```

**只包装 cloud adapter**, 不影响 mock 路径 (Phase B B.3/B.4 mock 性能不变).

### 为什么不修 httplib/OpenSSL (方案 B)

- 根因可能在 httplib 上游库, 修复需要 fork + patch + 升级周期 (数周)
- OpenSSL 1.1+ 自带线程安全, 但 `SSL_CTX` 在某些边缘场景仍需显式锁初始化
- 排查时间 1-3 天, 而本 change 阻塞 Phase B/E/G ship

**follow-up note** (本 change archive 时记入):
- 升级到 OpenSSL 3.0 + httplib 最新版
- 移除 SerializingDecorator (真正支持并发)
- 见 ADR-0087 "Cloud adapter threading model" (追踪 change: cloud-adapter-threading-root-cause)

## 测试设计

### Unit tests (`tests/test_serializing_decorator.cpp` 新建)

```cpp
TEST_CASE("SerializingDecorator forwards generate result") {
  // 1 worker, 串行调用 → result 透传
}

TEST_CASE("SerializingDecorator serializes concurrent generate calls") {
  // 2 worker, 同时提交 → inner 收到调用是串行的 (用 timing 或 counter 验证)
  // 验证: 第二个调用等第一个返回后才执行 inner->generate
}

TEST_CASE("SerializingDecorator honors stop_token during wait") {
  // 启动一个长 generate (mock sleep), 第二个调用在 cv 上 wait
  // 第二个调用的 stop_token request_stop → 立即返回 Cancelled error
}

TEST_CASE("SerializingDecorator available_models delegates") {
  // 验证 available_models() 透传
}
```

### Integration test (扩展 `tests/test_llm_provider_factory.cpp` 或新建)

```cpp
TEST_CASE("LLMProviderFactory.create deepseek wraps SerializingDecorator",
          "[realllm][multithread]") {
  require_real_llm_env();
  if (real_llm_env_skipped()) { SUCCEED("skipped"); return; }
  // 创建 provider, assert get_inner() 类型是 SerializingDecorator
}
```

### Stress test (`tests/test_cloud_adapter_multithread.cpp` 新建)

```cpp
TEST_CASE("CloudLLMAdapter 8 workers x 20 real LLM tasks no SIGSEGV",
          "[realllm][stress][multithread]") {
  require_real_llm_env();
  if (real_llm_env_skipped()) {
    WARN("stress test skipped (needs real LLM)");
    SUCCEED("skipped");
    return;
  }
  // DomainWorkerPool(8) + 共享 provider (经 SerializingDecorator 包装)
  // submit 160 tasks
  // 断言: 160 completed 全部 ok, 零 SIGSEGV, 总耗时 < 5 分钟 (串行但可接受)
}
```

### 解锁 B.2 (real-llm-core-coverage Phase B)

修改 `tests/test_domain_worker_pool.cpp` B.2:
- 删除 `WARN + SUCCEED` 占位
- 改回 `pool(4, bus)` (4 worker 并发)
- 删除 SKIP/WARN 注释

## Telemetry 副作用

`SerializingDecorator` 在 bus 上**不发射事件** (设计为透明), 与其他装饰器
(TracingDecorator) 不同. 副作用:
- ✅ 不影响事件流 (不插入新 topic)
- ✅ 不影响 cost 记录 (CostTrackingDecorator 仍记录, model 来自 req.params.model,
  详见 `fix-generation-request-model-default`)
- ✅ 不影响 audit (ComplianceDecorator 同上)

## 兼容性

**BREAKING 行为**: 多线程 CloudLLMAdapter 调用者**实际并发数从 N 降为 1**.
无现有调用方**依赖**此并发 (单 worker CognitiveWorker 串行; 单 worker Skill
子进程串行; ContextCompactor / GEPA 串行). **零观察面破坏** (行为仍正确).

**API**: `LLMProviderFactory::create` 返回类型不变 (仍是 `unique_ptr<ILLMProvider>`).
装饰链从 `CloudLLMAdapter` 变为 `SerializingDecorator → CloudLLMAdapter` — 装饰器
对外透明.

**测试**: 既有 `test_cloud_llm_live` 等单线程 LLM 测试不受影响.

## 实施顺序

1. **Phase 1 (代码)**:
   - 新建 `src/common/llm/serializing_decorator.h` + `.cpp`
   - 修改 `src/common/llm/llm_provider_factory.cpp` (1 处)
   - 修改 `tests/CMakeLists.txt` (新 test_serializing_decorator target)
2. **Phase 2 (测试)**:
   - 4 unit (decorator 行为) + 1 integration + 1 stress
   - 启用 B.2 (移除 SKIP)
3. **Phase 3 (验证)**:
   - ctest 全量 + 真实 deepseek stress + openspec validate + archive

每步骤独立 commit.
