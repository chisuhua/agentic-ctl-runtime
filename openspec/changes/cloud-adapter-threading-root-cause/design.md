# Design — cloud-adapter-threading-root-cause

## 追踪物架构

本 change 是**追踪物 (tracking artifact)**, 不是实施 change. 仅创建:
1. ADR 文档 (替代占位符 ADR-XXXX)
2. 升级路径记录
3. 现有代码中 ADR-XXXX 占位符引用更新

## 根因诊断 (从 fix-cloud-adapter-multithreading 继承)

gdb backtrace (commit c0cb522 Phase B ship):
```
Thread N received signal SIGSEGV
#0  std::function<void(int)>::operator()() const (this=0x11, ...)
#1  httplib::detail::create_client_socket(...)
#2  httplib::ClientImpl::send_(...)
#3  httplib::Client::Post(...)
#4  CloudLLMAdapter::do_post(...)
#5  ILLMProvider::generate(...)
```

观察:
- 触发条件: N≥2 worker 并发 + Authorization header + https 三者同时
- 单线程 + 真实 deepseek (A.2 ship): ✅ PASS
- 1 worker + 真实 deepseek (B.2 pool(1)): ✅ PASS
- 4 worker + mock provider (B.3, B.4): ✅ PASS
- HttpLLMAdapter 单独模式 (test_http_adapter): ✅ PASS
- 4 worker + 真实 deepseek (B.2 pool(4)): ❌ **SIGSEGV**

## 升级路径 (实施时)

### Step 1: OpenSSL 3.0 集成
- CMake: `find_package(OpenSSL 3.0 REQUIRED)`
- 链接: `target_link_libraries(... OpenSSL::SSL OpenSSL::Crypto)`
- 验证: SSL_CTX per-thread 正确初始化

### Step 2: httplib 升级
- upstream commit 检查: 多线程 Authorization header fix 是否纳入
- 替换 `external/httplib/httplib.h` 为 upstream 最新版
- 验证: httplib::Client 多线程语义

### Step 3: 现有测试验证
- Phase B B.2 (4 worker 真并发) — 期望无 SerializingDecorator 也通过
- Phase E Skill IPC (多 worker llm_generate)
- Phase G ContextCompactor (多 worker 摘要)

### Step 4: 移除默认包装
- `llm_provider_factory.cpp` cloud 路径去掉 `SerializingDecorator`
- SerializingDecorator 保留作为 OPT-IN (诊断/降级用途)

### Step 5: 性能 benchmark
- 4 worker 并发 deepseek: baseline (含 SerializingDecorator 串行) vs fix 后
- 期望: 并发性能恢复 (~3s vs ~12s)

## 兼容性

**BREAKING**: OpenSSL 3.0 升级可能影响外部使用 OpenSSL 1.1 的下游项目
- 缓解: ABI 兼容层 (OpenSSL 3.0 默认提供)
- 文档: 在 ADR 中明确记录 BREAKING

**OPT-IN SerializingDecorator**: 升级后 SerializingDecorator 仍可用
- `LLMProviderFactory::create(config, opts)` opts.serializer = true
- 默认 false (升级后无需串行化)

## 实施顺序 (未来 change)

1. ADR 创建 (本 change 完成)
2. OpenSSL 3.0 集成 (独立 change)
3. httplib 升级 (独立 change)
4. 验证 + 移除默认包装 (独立 change)
5. benchmark + OPT-IN 设计 (独立 change)

每步骤独立 commit 保留回溯能力.