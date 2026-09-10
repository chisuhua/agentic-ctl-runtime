# ADR-0087 Sprint 24 — OpenSSL 3.0 ABI 审计报告

**日期**: 2026-09-10
**调研人**: Sisyphus session (Wave 4 series ship 后)
**目的**: 验证 ADR-0087 §Step 1 "OpenSSL 1.1+ → 3.0 集成" 假设 + 评估实际迁移 LOC
**结论先报**: **0 LOC migration** — ADR-0087 §Step 1 假设**不成立**,OpenSSL 3.0 集成已隐式完成

---

## 1. 调研背景 (per handoff §3.1)

ADR-0087 §根因诊断 (line 78-82) 假设:
> "OpenSSL `SSL_CTX` 多线程初始化竞态 — httplib 或 OpenSSL 默认 SSL_CTX
> 不是 per-thread, 多线程同时首次建立 SSL 连接触发 `SSL_new` /
> `SSL_CTX_use_certificate` 等内部状态 corruption"

ADR-0087 §Step 1 (line 88-94) 升级计划:
```cmake
find_package(OpenSSL 3.0 REQUIRED)
target_link_libraries(agenticdsl_common PUBLIC OpenSSL::SSL OpenSSL::Crypto)
```

本审计验证以上假设 + 估算真实迁移 LOC。

---

## 2. 调研方法

### 2.1 本地 grep (handoff §5.2 step 1)

```bash
grep -n "SSL_CTX\|SSL_new\|SSL_connect\|BIO_new\|EVP_\|X509_\|SSL_read\|SSL_write\|TLS_\|OPENSSL_" \
     src/common/llm/cloud_adapter.cpp
```

### 2.2 系统 OpenSSL 版本探测

```bash
cmake -LA build | grep -i openssl
openssl version
```

### 2.3 远程 Librarian 调研 (background `bg_50550405`)

Web 搜索 OpenSSL 3.0 Migration Guide + 主流 OSS 迁移踩坑 + thread_local SSL_CTX 模式。

---

## 3. 关键发现 (核心反常)

### 3.1 `cloud_adapter.cpp` **0 行 OpenSSL 代码**

**实证 grep 结果**: 完全无匹配。

**cloud_adapter.cpp 真实 OpenSSL 接触面** (line 11-15):
```cpp
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>     // ← 唯一 OpenSSL 接触点
```

**所有 HTTPS 操作通过 `httplib::Client` 封装** (line 244, 265):
```cpp
httplib::Client cli(config_.api_url);     // 内部维护 SSL_CTX
cli.Post(endpoint, headers, json_body, "application/json");
```

**结论**:
- ADR-0087 §根因诊断 "OpenSSL `SSL_CTX` 多线程初始化竞态" **不适用 cloud_adapter.cpp**
- OpenSSL API 调用封装在 cpp-httplib 内部,本项目代码**间接**通过 httplib 受影响
- 任何 OpenSSL 升级实际影响面 = **0 LOC** (代码无直接 API 调用)

### 3.2 系统 OpenSSL 已是 3.0.13

**实证**:
```
$ cmake -LA build | grep -i openssl
OPENSSL_CRYPTO_LIBRARY:FILEPATH=/usr/lib/x86_64-linux-gnu/libcrypto.so
OPENSSL_SSL_LIBRARY:FILEPATH=/usr/lib/x86_64-linux-gnu/libssl.so

$ openssl version
OpenSSL 3.0.13 30 Jan 2024 (Library: OpenSSL 3.0.13 30 Jan 2024)
```

**关键事实**:
- `CMakeLists.txt:25` 已用 `find_package(OpenSSL REQUIRED)` (无版本约束)
- `find_package(OpenSSL REQUIRED)` 自动找系统**最高**版本 (3.0.13)
- 默认 provider + 1.1 兼容层开启,无需任何配置

**结论**:
- ADR-0087 §Step 1 line 92 `find_package(OpenSSL 3.0 REQUIRED)` **冗余** (无版本约束已隐式 3.0+)
- ADR-0087 §Step 1 line 93 `target_link_libraries(agenticdsl_common PUBLIC OpenSSL::SSL OpenSSL::Crypto)` 当前**已隐式应用** (httplib 通过 cpp-httplib 引入)

### 3.3 ADR-0087 §Step 1 → **0 LOC migration**

| 子项 | ADR-0087 计划 | 实际状态 | LOC |
|------|------------|--------|-----|
| CMake `find_package(OpenSSL 3.0 REQUIRED)` | 显式加 3.0 | 已 `find_package(OpenSSL REQUIRED)`,自动 3.0.13 | **0** |
| `target_link_libraries` | 加 `OpenSSL::SSL OpenSSL::Crypto` | 隐式通过 httplib 链接 | **0** |
| SSL_CTX per-thread 初始化 | 需新代码 | 代码无 direct SSL_CTX 调用 | **0** |
| `SSL_CTX_new_ex` 等 3.0 新 API | 旧 API 兼容层即可 | 无需切换 | **0** |
| `ERR_load_*` 删除 | 1.1 已 deprecated,3.0 自动加载 | 代码无此调用 | **0** |

**总计**: ADR-0087 §Step 1 完整 ship **0 LOC**(无需任何代码改动)。

---

## 4. OpenSSL 3.0 ABI 已知 break points (librarian 调研)

虽然本项目 0 LOC 迁移,但 OpenSSL 3.0 ABI 知识对 Sprint 27+ 升级 (跨项目下游消费方) 仍需记录。

### 4.1 Provider 架构 (核心变化)

[OpenSSL Migration Guide](https://docs.openssl.org/master/man7/ossl-guide-migration/):
> "All algorithm implementations available via providers are accessed through the 'high level' APIs (for example those functions prefixed with EVP). They cannot be accessed using the 'Low Level APIs'."

**对本项目影响**: 无 (代码无 OpenSSL 调用)。

### 4.2 Low-Level API 废弃 (DEPRECATED)

[OpenSSL 3.0.0 Release Notes](https://github.com/openssl/openssl/releases/tag/openssl-3.0.0) 列出:
- 所有 low-level MD/SHA digest 函数 (`SHA1_Init` 等)
- 所有 low-level AES/DES/RC cipher 函数 (`AES_encrypt` 等)
- 所有 low-level DH/DSA/ECDH/ECDSA/RSA 公钥函数 (`RSA_new` 等)
- `ENGINE_*` API
- `ERR_load_*()` 函数

**对本项目影响**: 无 (httplib 已用 high-level EVP API)。

### 4.3 Per-thread SSL_CTX 模式 (OpenSSL 3.0 合法)

[OpenSSL threads documentation](https://docs.openssl.org/master/man7/openssl-threads/):
> "An SSL_CTX object should not be changed after it is used to create any SSL objects or from multiple threads concurrently, since the implementation does not provide serialization of access for these cases."
> "Each thread handling TLS connections in parallel should create its own SSL object from the shared SSL_CTX."

**真实项目证据** ([mediasoup DtlsTransport.cpp](https://github.com/versatica/mediasoup/blob/v3/worker/src/RTC/DtlsTransport.cpp)):
```cpp
thread_local X509* DtlsTransport::certificate{ nullptr };
thread_local EVP_PKEY* DtlsTransport::privateKey{ nullptr };
thread_local SSL_CTX* DtlsTransport::sslCtx{ nullptr };
```

**对本项目意义**: 如果未来 httplib 出现 SSL_CTX 多线程问题,`thread_local SSL_CTX*` 是 OpenSSL 3.0 推荐的修法。但**当前 vendored httplib 0.18.4 已通过 ctx_mutex 保护 SSL_CTX (line 8940-8968 ssl_new)**,无需该模式。

### 4.4 性能踩坑 (参考,但非本审计重点)

- **HAProxy** ([openssl#23388](https://github.com/openssl/openssl/issues/23388)): 3.0 多线程性能下降 27%,根因 `evp_generic_fetch` 锁竞争
- **Nginx** ([openssl#21833](https://github.com/openssl/openssl/issues/21833)): 类似问题
- **Curl** ([discussion#11279](https://github.com/curl/curl/discussions/11279)): TLS 1.0/1.1 默认禁用,需 `SSL_CTX_set_security_level(ctx, 0)`

**对本项目影响**: 无 (代码无 OpenSSL 调用,httplib 内部性能由其升级解决)。

---

## 5. ADR-0087 §Step 1 假设验证

| ADR-0087 §Step 1 假设 | 实际状态 | 验证 |
|---------------------|---------|------|
| "OpenSSL 1.1+ SSL_CTX 多线程初始化竞态" | cloud_adapter.cpp 无 SSL_CTX 调用 | ❌ 不适用 |
| "需 `find_package(OpenSSL 3.0 REQUIRED)`" | 已隐式 3.0.13 | ❌ 冗余 |
| "SSL_CTX per-thread 正确初始化" | httplib 0.18.4 已 ctx_mutex 保护 | ❌ 已在 httplib |
| "现有 SSL 调用代码 ABI 兼容" | 无 SSL 调用 | ❌ 不适用 |

**结论**: ADR-0087 §Step 1 全部假设均不适用,**应从 ADR 升级路径中删除或重写**。

---

## 6. 修正建议

### 6.1 立即 (Sprint 24)

- **删除 ADR-0087 §Step 1 整段** (line 88-99) — 假设不成立,迁移 0 LOC
- **修订 ADR §根因诊断** — 真实根因待 httplib audit 报告 + Sprint 26 实证重诊断
- **ADR-0087 翻牌** 🔍 Proposed → 🟡 Partial (调研完成)

### 6.2 Sprint 25 重定位

**原计划**: OpenSSL 3.0 集成 + per-thread SSL_CTX (1-1.5 天)
**建议改为**: **httplib 升级 0.18.4 → 0.39.0+ for CVE-2026-33745** (详见 httplib status audit)

**理由**:
- OpenSSL 集成已完成 (0 LOC)
- httplib 升级有 Critical security fix (CVE-2026-33745 auth redirect 凭证泄露)
- 资源更优先用于 security 而非性能

### 6.3 Sprint 26+ 待办

- 真正根因重诊断: 在 httplib 0.39.0 升级后,跑 8 worker × 20 task stress,看 SerializingDecorator 是否还需要
- 如果 0.39.0 已无 SIGSEGV → ADR-0087 §Decision 1 "默认包装" 可移除
- 如果仍 SIGSEGV → httplib 0.39.0 后根因更细,需 gdb 重诊断

---

## 7. 风险与缓解

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| Sprint 25 误改 httplib 升级范围 → OpenSSL 升级 → 0 收益 | 高 | 中 | 本审计 + handoff §5.2 明确告知 |
| 真实根因未明 → SerializingDecorator 移除后再次 SIGSEGV | 中 | 高 | Decision 3 已规划 OPT-IN serializer + 紧急降级 |
| 下游消费者 (hydraforge-pdk dual-repo) 仍用 OpenSSL 1.1 | 低 | 中 | ADR §兼容性章节已记录 BREAKING |

---

## 8. 参考资料

### 8.1 OpenSSL 官方文档

- [OpenSSL 3.0 Migration Guide (master)](https://docs.openssl.org/master/man7/ossl-guide-migration/)
- [OpenSSL 3.0.0 Release Notes](https://github.com/openssl/openssl/releases/tag/openssl-3.0.0)
- [OpenSSL Threads Documentation](https://docs.openssl.org/master/man7/openssl-threads/)
- [CMake FindOpenSSL](https://cmake.org/cmake/help/latest/module/FindOpenSSL.html)

### 8.2 OSS 迁移踩坑

- [HAProxy Performance Issue #23388](https://github.com/openssl/openssl/issues/23388)
- [Nginx Performance Issue #21833](https://github.com/openssl/openssl/issues/21833)
- [Curl TLS Version Discussion #11279](https://github.com/curl/curl/discussions/11279)
- [mediasoup thread_local SSL_CTX usage](https://github.com/versatica/mediasoup/blob/v3/worker/src/RTC/DtlsTransport.cpp)

### 8.3 项目内部引用

- ADR-0087 §根因诊断: `docs/adr/adr-0087-cloud-adapter-threading-model.md` line 54-84
- ADR-0087 §Step 1: `docs/adr/adr-0087-cloud-adapter-threading-model.md` line 88-99
- c0cb522 Phase B gdb 实证: `git show c0cb522`
- handoff §3.1 调研任务: `docs/handoff/2026-09-10-adr-0087-sprint-24-research-handoff.md` line 64-92
- 关联审计: `docs/audits/2026-09-10-adr-0087-sprint-24-httplib-status.md`

---

**审计结论**: ADR-0087 §Step 1 假设不成立,**0 LOC migration**,OpenSSL 3.0 集成已隐式完成。建议修订 ADR §Step 1 + 重定位 Sprint 25 范围至 httplib 升级 for CVE-2026-33745 (详见 httplib status audit)。
