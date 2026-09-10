## ADDED Requirements

### Requirement: CloudLLMAdapter 多线程安全 (OpenSSL 3.0)

`CloudLLMAdapter` SHALL 在 N≥2 worker 并发 + Authorization header + https 场景下, 不触发栈 corruption (create_client_socket SIGSEGV). SHALL 通过 OpenSSL 3.0 per-thread SSL_CTX 初始化或 thread-local 缓存实现.

#### Scenario: 4 worker 真并发
- WHEN 4 worker 并发调用 `CloudLLMAdapter::generate()` 各自带 Authorization header + https
- THEN 4 个调用全部成功返回 (无 SIGSEGV, 无 race condition)
- AND 性能 ≤ baseline 串行时长的 1/2 (4× 加速)

### Requirement: httplib 多线程 Authorization header 安全

`external/cpp-httplib/httplib.h` SHALL 升级到包含 issue #1903 fix 的 upstream release. Authorization header 在多线程并发场景下 SHALL 线程安全.

#### Scenario: 4 worker 并发 Authorization header
- WHEN 4 worker 并发调用 `httplib::Client::Post()` 各自带不同 Authorization header
- THEN 4 个调用的 Authorization header 各自正确传递 (无 cross-contamination)

### Requirement: SerializingDecorator OPT-IN 降级

`LLMProviderFactory::create(config, opts)` SHALL 在 cloud 路径默认不注入 SerializingDecorator (升级成功后). `opts.serializer = true` SHALL 显式启用 SerializingDecorator 包装 (诊断 + 紧急降级).

#### Scenario: 默认路径无串行化
- WHEN `LLMProviderFactory::create(config, opts)` opts.serializer 默认 false
- THEN 返回的 cloud provider 无 SerializingDecorator 包装
- AND 多 worker 并发调用直连 CloudLLMAdapter

#### Scenario: OPT-IN 路径串行化
- WHEN opts.serializer = true 显式启用
- THEN 返回的 cloud provider 含 SerializingDecorator 包装
- AND 多 worker 并发调用被串行化 (mutex + cv)

### Requirement: 回归守卫

Phase B/E/G 现有测试 SHALL 全部 PASS, 验证升级期间无回归:
- Phase B B.2: `tests/test_cloud_adapter_multithread.cpp` 4 worker 真并发 (Wave 1 #2 已 ship)
- Phase E: `tests/test_skill_interpreter.cpp` 7.8b/7.8c (Wave 4 ship)
- Phase G: `tests/test_context_compactor.cpp` 多 worker (既有)

#### Scenario: 升级期间回归验证
- WHEN 升级任一步骤 (OpenSSL / httplib / 移除默认 SerializingDecorator)
- THEN 全量 ctest 228/228 PASS 零回归
- AND Phase B/E/G 测试全部 PASS

### Requirement: Benchmark 性能 baseline

升级完成后 4 worker 并发 deepseek benchmark SHALL 达到 ~4× 加速 (baseline 串行 ~12s → fix 后 ~3s).

#### Scenario: 4 worker 并发 deepseek
- WHEN 4 worker 并发调用 deepseek LLM API (Authorization header + https)
- THEN 总 wall time ≤ baseline 串行时长的 1/2
- AND 单个 LLM 调用时长不变 (LLM API 网络 I/O 占主导, 不应被多 worker 拖慢)