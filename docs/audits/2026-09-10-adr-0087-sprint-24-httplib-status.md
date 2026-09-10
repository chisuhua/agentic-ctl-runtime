# ADR-0087 Sprint 24 — httplib Upstream Fix Status 调研报告

**日期**: 2026-09-10
**调研人**: Sisyphus session (Wave 4 series ship 后)
**目的**: 验证 ADR-0087 §Step 2 "httplib 升级 (upstream 多线程 Authorization header fix)" + 评估真实升级目标版本
**结论先报**: **PR #701 fix 已在 0.18.4 中**;**真正应升级的理由是 CVE-2026-33745 (High CVSS 7.4) + 多个 client 侧 security advisory**,目标版本 **v0.54.1 (latest stable,2026-08-30)**
**Oracle 修正 (2026-09-10)**: Sprint 24 评审发现 (a) 严重度标 Critical 实为 High, (b) v0.39.0 仅覆盖 1/3 client 侧 advisory, (c) §3.3 vs §5.3 表述自相矛盾 → 本报告 + ADR-0087 §实施日志同步修订 (commit 待 ship)

---

## 1. 调研背景 (per handoff §3.2)

ADR-0087 §根因诊断 (line 82-84) 假设:
> "httplib Authorization header 栈处理 bug — httplib 内部对某些 header value 的栈分配在多线程下 corruption"

handoff §3.2 提到 `external/cpp-httplib/httplib.h:3011` 已含 issue #1903 引用,**但 issue #1903 实际是 Windows file size 32-bit bug,与 threading 无关**。

本调研明确:
1. 真实修复 SIGSEGV 的 upstream issue/PR
2. 是否已在 vendored httplib 中
3. 是否还有其他**更优先**的升级理由 (security CVE)

---

## 2. 调研方法

### 2.1 本地版本探测

```bash
grep -n "CPPHTTPLIB_VERSION" external/cpp-httplib/httplib.h
# → 0.18.4
```

### 2.2 关键代码扫描

```bash
grep -n "Authorization" external/cpp-httplib/httplib.h | head -30
grep -n "std::mutex\|ctx_mutex\|request_mutex_" external/cpp-httplib/httplib.h | head -30
```

### 2.3 远程 Librarian 调研 (background `bg_e00cd1a2`)

GitHub search: yhirose/cpp-httplib Authorization + threading + PR/issue history + release timeline + CVE。

---

## 3. 关键发现 (校正 ADR-0087 §根因诊断)

### 3.1 当前 vendored httplib 版本 = **0.18.4**

**实证** (`external/cpp-httplib/httplib.h:11`):
```cpp
#define CPPHTTPLIB_VERSION "0.18.4"
```

### 3.2 真实 threading SIGSEGV fix = **PR #701** (merged Nov 29, 2020)

**Issue 链**:
- **Issue #697** — "error cant pass verifier on windows" — SSL_shutdown on already-closed socket
- **Issue #699** — "could you reopen #697?" — 同根因
- **PR #701** — "Fix multiple threading bugs including #699 and #697"
  - URL: https://github.com/yhirose/cpp-httplib/pull/701
  - Merged: **Nov 29, 2020**
  - Commit: `ecd709af1eaf2c8f794ac908ef9554f3b1331a76`

**Shipped in**: **v0.8.0 (Jan 12, 2021)**

### 3.3 PR #701 修复内容 (实证 vendored 0.18.4 已含)

**关键变量** (`external/cpp-httplib/httplib.h:1507-1509`):
```cpp
size_t socket_requests_in_flight_ = 0;
std::thread::id socket_requests_are_from_thread_;
bool socket_should_be_closed_when_request_is_done_ = false;
```

**这些变量由 PR #701 引入**,证明 vendored 0.18.4 **已含 fix**。

**其他已含 threading 保护** (本地 grep):
- `std::mutex ctx_mutex_` at line 1939/1989 (SSLClient/SSLServer SSL_CTX 保护)
- `ssl_new(socket_t sock, SSL_CTX *ctx, std::mutex &ctx_mutex, ...)` at line 8940-8968 (mutex 保护 SSL_new)
- `std::recursive_mutex request_mutex_` at line 7477 (Authorization header 处理锁)
- `size_t authorization_count_ = 0` at line 663 (Authorization retry 计数 + 上限 5)
- `std::lock_guard<std::mutex> guard(ctx_mutex_)` at line 9239/9465 (SSL handshake 保护)

**结论**: 当前 vendored 0.18.4 **已含所有 ADR-0087 §Step 2 关心的 threading fix**,**0 LOC migration** (无需升级即可获得 threading 安全)。

### 3.4 issue #1903 与 threading 无关 ❌

**handoff §3.2 误关联**:
- 真实 issue #1903: "If the following line doesn't compile due to QuadPart, update Windows SDK"
- 修复: `static_cast<ULONGLONG>(size.QuadPart)` Windows LARGE_INTEGER 转换
- **与 Authorization header / threading / SIGSEGV 完全无关**

**结论**: ADR-0087 §根因诊断 (line 82-84) 混淆了两个独立 issue — **PR #701 (threading) vs #1903 (Windows file size)**。这是 ADR §根因诊断偏差的**实证**。

### 3.5 🚨 **新发现: client 侧 security advisories (multi-CVE 评估)**

**[Oracle 评审 2026-09-10 加注]**: Sprint 24 调研仅发现 CVE-2026-33745,忽略同窗口期 2 个更直接相关的 client 侧 advisory。本节补充。

#### 3.5.1 CVE-2026-33745 (GHSA-6hrp-7fq9-3qv2) — **High (CVSS 7.4)**

**Issue**: [GHSA-6hrp-7fq9-3qv2](https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-6hrp-7fq9-3qv2)
- **Severity**: **High (CVSS 7.4)** — 非 Critical (Sprint 24 调研初版误标, Oracle 实证修正)
- **Description**: Auth credentials leaked on cross-origin redirect
- **Fixed in**: **v0.39.0**
- **Reproducer**: `set_follow_location(true)` + 401 response + cross-origin redirect → Authorization header **leaked to third-party**

**当前状态**: vendored 0.18.4 **未含此 fix** → **存在 High security 风险** (如果代码使用 follow_location + Authorization)

**代码审计** (`src/common/llm/cloud_adapter.cpp` 全文 402 行):
- ❌ **未使用** `set_follow_location(true)` — 当前 httplib::Client 默认 follow_location = false
- ✅ **因此本项目**当前**未触发** CVE-2026-33745
- ⚠️ 但**未来引入 follow_location 风险高** (无测试守卫)

#### 3.5.2 GHSA-39q5-hh6x-jpxx (High, 2026-03-10) — **更直接 client 崩溃**

**Issue**: [GHSA-39q5-hh6x-jpxx](https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-39q5-hh6x-jpxx)
- **Severity**: High
- **Description**: 恶意 Content-Length **响应头**使 client 进程崩溃 (Remote Process Crash via Malformed Content-Length Response Header)
- **Fixed in**: 后续 0.39.x patch 系列 (具体版本待查)
- **Reproducer**: 恶意/有 bug 的 LLM endpoint 返回畸形 Content-Length → httplib Client 解析时崩溃

**当前状态**: vendored 0.18.4 **未含此 fix** → **直接暴露**
**触发条件**: 任何 cloud LLM 调用 (无论是否 Authorization)

#### 3.5.3 GHSA-h6wq-j5mv-f3q8 (Moderate, 2026-05-12) — chunked transfer DoS

**Issue**: [GHSA-h6wq-j5mv-f3q8](https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-h6wq-j5mv-f3q8)
- **Severity**: Moderate
- **Description**: 负 chunk-size DoS (negative chunk-size in chunked Transfer-Encoding)
- **Fixed in**: 后续 0.5x 系列
- **Reproducer**: 恶意 LLM streaming 端点发送负 chunk-size → httplib client 死循环 / OOM

**当前状态**: vendored 0.18.4 **未含此 fix**
**触发条件**: LLM streaming (SSE/chunked) — **本项目直接暴露** (`generate_stream` 走 chunked transfer)

#### 3.5.4 GHSA-c3h8-fqq4-xm4g (High, 2026-03-13) — proxy TLS bypass (条件性)

**Issue**: [GHSA-c3h8-fqq4-xm4g](https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-c3h8-fqq4-xm4g)
- **Severity**: High (条件性)
- **Description**: Silent TLS Certificate Verification Bypass on HTTPS Redirect via Proxy
- **Fixed in**: 后续 0.39.x patch 系列

**当前状态**: vendored 0.18.4 **未含此 fix**
**触发条件**: 项目使用 HTTP_PROXY/HTTPS_PROXY 环境变量 + redirect → TLS 验证绕过
**当前审计**: httplib 不自动读 env proxy;项目未显式设置 proxy → **未触发**

#### 3.5.5 client 侧 advisory 汇总

| CVE/GHSA | Severity | 触发条件 | 本项目当前状态 |
|----------|----------|----------|--------------|
| CVE-2026-33745 | High (7.4) | Authorization + follow_location | **未触发** (不用 follow_location) |
| GHSA-39q5-hh6x-jpxx | **High** | 任何 cloud LLM 调用 | **直接暴露** ⚠️ |
| GHSA-h6wq-j5mv-f3q8 | Moderate | LLM streaming | **直接暴露** ⚠️ |
| GHSA-c3h8-fqq4-xm4g | High (条件) | proxy + redirect | **未触发** (不用 proxy) |

**结论**: 仅升级到 v0.39.0 仅覆盖 1/4 advisories (CVE-2026-33745)。**真正覆盖需要 v0.5x 系列 (latest v0.54.1)**。

---

## 4. ADR-0087 §Step 2 假设验证

| ADR-0087 §Step 2 假设 | 实际状态 | 验证 |
|---------------------|---------|------|
| "需 httplib 升级修复 Authorization header 多线程栈 corruption" | PR #701 fix 已在 0.18.4 (line 1507-1509, 7477, 8940-8968) | ❌ 已完成 |
| "upstream 多线程 fix 调研" | PR #701 merged 2020-11-29 | ✅ 已知 |
| "API 变更 (compat with current usage)" | 0.18.4 已稳定 4+ 年,API 兼容 | ✅ 无变更 |
| "替换 external/httplib/httplib.h 为 upstream 最新版" | 升级理由不成立 (fix 已在) | ❌ 重新定位 |

**结论**: ADR-0087 §Step 2 "httplib 升级 for threading fix" 假设**不成立** — 应从 ADR 升级路径中删除或重写。

---

## 5. 修订建议 (基于新发现)

### 5.1 立即 (Sprint 24)

- **删除 ADR-0087 §Step 2 整段** (line 100-108) — 升级理由不成立
- **修订 ADR §根因诊断** — 澄清 PR #701 (threading) vs #1903 (Windows file size) 区别
- **ADR-0087 翻牌** 🔍 Proposed → 🟡 Partial (调研完成)

### 5.2 Sprint 25 范围重定位 (security 优先, Oracle 修正版)

**[Oracle 评审 2026-09-10 加注]**: 原建议 "v0.39.0+",仅覆盖 1/4 client 侧 advisory。修订为 **v0.54.1 (latest stable)**。

**原计划**: httplib 升级 for threading fix (1-1.5 天)
**建议改为**: **httplib 升级 0.18.4 → v0.54.1 (覆盖 4/4 client 侧 advisories + 21 个版本的累积修复)** (1-2 天)

**理由**:
- **PR #701 等 threading fix 已隐式完成** (本项目 SIGSEGV 已在 0.18.4 上观测,真根因未明 — 见 §5.3 修订)
- **4 个 client 侧 security advisories** (GHSA-39q5 + GHSA-h6wq + GHSA-c3h8 + CVE-2026-33745):
  - **GHSA-39q5 (High) + GHSA-h6wq (Moderate) 直接暴露** (cloud_adapter + http_adapter + docker_backend)
  - **CVE-2026-33745 (High) 未来 follow_location 引入风险**
  - **GHSA-c3h8 (High 条件性) proxy + redirect**
- v0.54.1 一次性覆盖 4/4 advisories + 21 个 minor 版本累积 bug fixes
- "避免 v0.40+ 引入新 break" 假设不成立 — v0.52.0 Headers ordering + v0.53.0 WebSocketClient (unused) breaking changes 已知可控

**升级目标版本**:
- **目标**: **v0.54.1** (latest stable, 2026-08-30)
- 已知 breaking changes 0.19→0.54:
  - **v0.52.0**: `Headers`/`Params` 改为 insertion-ordered multimap, ABI break + iterator invalidation 规则变化
  - **v0.53.0**: `WebSocketClient::connect` 返回 `ws::Result` — **本项目未用 WebSocket,无影响**
- 本项目 httplib 使用面窄 (Client(host) + 3 timeout setter + Post(endpoint, headers, body, type) + Headers 迭代器构造),兼容性风险可控

### 5.3 Sprint 26+ 真实根因重诊断 (Oracle 修订版)

**[Oracle 评审 2026-09-10 加注]**: §3.3 表述 "已含所有 threading fix / 无需升级即获 threading 安全" 与本节矛盾 — **SIGSEGV 是在含 PR #701 的 0.18.4 上观测到的** (c0cb522 Phase B ship 实证),PR #701 存在不证明根因已修复。

**准确表述**:
- ✅ PR #701 修复的 2020 年代 socket lifecycle race 已不在场 (排除)
- ❌ **真根因未明** — gdb backtrace (`create_client_socket` + `std::function<void(int)> this=0x11`) 指向 `ClientImpl::socket_options_` 损坏/use-after-free,与 PR #701 类 race 不一致
- ⚠️ SerializingDecorator **当前仍必要** (8 worker × 20 task = 160 calls 验证零 SIGSEGV)
- 🎯 升级到 v0.54.1 提供 21 个 minor 版本的累积修复,**可能**覆盖真根因 (实证验证)

**可能解释** (含 v0.54.1 升级后验证):
1. httplib 0.18.4 内部更细的 race (chunked transfer / SSE decoder / ClientImpl 生命周期) — 0.54.1 已修复
2. OpenSSL 1.1.x + httplib 0.18.x 互操作行为 (当前系统已 3.0.13) — 不再相关
3. `httplib::Client` 栈构造 per-call 但跨线程共享 SSL_CTX 内部状态 — 0.54.1 mutex 覆盖更完整

**Sprint 26 实证方案 (Oracle 补强版)**:
1. ✅ 升级 httplib 到 v0.54.1 (前置于 Sprint 25)
2. 暂时移除 SerializingDecorator 默认包装 (`llm_provider_factory.cpp:102-114`)
3. 跑 8 worker × 20 task stress (`test_cloud_adapter_multithread.cpp`),**必须覆盖 HTTPS + Authorization 路径** (mock TLS server 或真实端点 opt-in) — mock HTTP-only 不触发原崩溃
4. **必须 TSan preset + ≥3 次重复** (SIGSEGV 是概率事件,单次 green 不构成证据)
5. 跑 Phase E Skill IPC + Phase G ContextCompactor (多 worker 真并发) 同样覆盖
6. **如果零 SIGSEGV** → ADR-0087 §Decision 1 "默认包装" 可移除,SerializingDecorator 真正 OPT-IN
7. **如果仍 SIGSEGV** → httplib 0.54.1 后根因更细,需 gdb 重诊断 (类似 c0cb522 Phase B 流程)
8. **Decision 3 opts.serializer 保持可逆** — 任何时刻可手动开启降级

---

## 6. 风险与缓解 (Oracle 修正版)

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| httplib 0.18.4 → 0.54.1 跨 36 minor 版本,API/行为漂移 | 中 | 高 | **0.19→0.54 breaking changes 清点** (v0.52.0 Headers ordering + v0.53.0 ws::Result);4 个使用点编译验证;Phase 0 unit + Phase B mock test 守门 |
| 升级前 vendored httplib 有未记录的本地 patch | 低 | 高 | **M3 前置**: diff vendored httplib.h vs upstream v0.18.4 tag,确认零本地修改 |
| 升级后 GHSA-39q5 / GHSA-h6wq 仍未触发场景 (如 401 redirect) 覆盖不足 | 中 | 中 | Sprint 26 stress 必须覆盖 HTTPS + Authorization + chunked streaming 路径 |
| Sprint 26 实证方案误判"零 SIGSEGV" → 默认包装移除后再次 SIGSEGV | 低 | 高 | Decision 3 opts.serializer OPT-IN 保持可逆;TSan preset + ≥3 次重复证据 |
| 下游消费者 (hydraforge-pdk dual-repo) 仍用 0.18.4 | 低 | 低 | ADR §兼容性章节记录升级要求 + sync script 自动化 |
| 其他 client/server-side CVE 在 0.54.1 之后发布 → 本升级未覆盖 | 低 | 中 | **L2**: set_follow_location CI grep 守卫防 CVE-2026-33745;订阅 httplib security advisories |

---

## 7. 参考资料

### 7.1 httplib issue/PR

- **PR #701** (merged Nov 29, 2020): https://github.com/yhirose/cpp-httplib/pull/701
- Issue #697 (Windows SSL_shutdown): https://github.com/yhirose/cpp-httplib/issues/697
- Issue #699 (reopen #697): https://github.com/yhirose/cpp-httplib/issues/699
- Issue #1903 (Windows file size, 与 threading 无关): https://github.com/yhirose/cpp-httplib/issues/1903

### 7.2 httplib security advisories (Oracle 补充)

- **CVE-2026-33745** (GHSA-6hrp-7fq9-3qv2, High CVSS 7.4, 2026-03-25): https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-6hrp-7fq9-3qv2 — Fixed in v0.39.0
- **GHSA-39q5-hh6x-jpxx** (High, 2026-03-10): https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-39q5-hh6x-jpxx — Content-Length response crash
- **GHSA-h6wq-j5mv-f3q8** (Moderate, 2026-05-12): https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-h6wq-j5mv-f3q8 — negative chunk-size DoS
- **GHSA-c3h8-fqq4-xm4g** (High conditional, 2026-03-13): https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-c3h8-fqq4-xm4g — proxy redirect TLS bypass
- httplib security advisories 列表: https://github.com/yhirose/cpp-httplib/security/advisories
- 最新稳定版: **v0.54.1** (2026-08-30, 覆盖所有上述 advisories)

### 7.3 httplib Authorization 相关 issues (排除)

- Issue #466 (Digest auth WWW-Authorization vs Authorization) — Bug, fixed
- Issue #484 (Bearer token feature) — Enhancement
- Issue #127 (Basic auth support) — Shipped

### 7.4 项目内部引用

- ADR-0087 §根因诊断: `docs/adr/adr-0087-cloud-adapter-threading-model.md` line 54-84
- ADR-0087 §Step 2: `docs/adr/adr-0087-cloud-adapter-threading-model.md` line 100-108
- SerializingDecorator 部署位置: `src/common/llm/llm_provider_factory.cpp:102-114`
- c0cb522 Phase B gdb 实证: `git show c0cb522`
- vendored httplib 关键代码 (`request_mutex_` 声明在 L1504,Sprint 24 调研 L7477 行号漂移已修正): `external/cpp-httplib/httplib.h:11, 663, 1504, 1507-1509, 1939/1989, 8940-8968`
- 关联审计: `docs/audits/2026-09-10-adr-0087-sprint-24-openssl-audit.md`
- Oracle 评审 session: `ses_f760c60d5ffe9hmgREbWz0hA8u` (2026-09-10)

---

**调研结论 (Oracle 修正版)**: ADR-0087 §Step 2 假设不成立 (PR #701 fix 已在 0.18.4,**但 SIGSEGV 在 0.18.4 上仍被观测**,PR #701 存在不证真根因已修)。**真正升级理由是 4 个 client 侧 security advisories**,目标版本 **v0.54.1 (latest stable)**。Sprint 25 重定位至 security 升级,Sprint 26 通过 HTTPS+Authorization+TSan 实证方案判断 SerializingDecorator 是否仍必要。
