# ADR-0087 Sprint 24 — httplib Upstream Fix Status 调研报告

**日期**: 2026-09-10
**调研人**: Sisyphus session (Wave 4 series ship 后)
**目的**: 验证 ADR-0087 §Step 2 "httplib 升级 (upstream 多线程 Authorization header fix)" + 评估真实升级目标版本
**结论先报**: **PR #701 fix 已在 0.18.4 中**;**真正应升级的理由是 CVE-2026-33745 (Critical security)**,目标版本 **v0.39.0+**

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

### 3.5 🚨 **新发现: CVE-2026-33745 (GHSA-6hrp-7fq9-3qv2)** — Critical security

**Issue**: [GHSA-6hrp-7fq9-3qv2](https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-6hrp-7fq9-3qv2)
- **Severity**: Critical
- **Description**: Auth credentials leaked on cross-origin redirect
- **Fixed in**: **v0.39.0**
- **Reproducer**: `set_follow_location(true)` + 401 response + cross-origin redirect → Authorization header **leaked to third-party**

**当前状态**: vendored 0.18.4 **未含此 fix** → **存在 Critical security 风险** (如果代码使用 follow_location + Authorization)

**代码审计** (`src/common/llm/cloud_adapter.cpp` 全文 402 行):
- ❌ **未使用** `set_follow_location(true)` — 当前 httplib::Client 默认 follow_location = false
- ✅ **因此本项目**当前**未触发** CVE-2026-33745
- ⚠️ 但**未来引入 follow_location 风险高** (无测试守卫)

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

### 5.2 Sprint 25 范围重定位 (security 优先)

**原计划**: httplib 升级 for threading fix (1-1.5 天)
**建议改为**: **httplib 升级 0.18.4 → v0.39.0+ for CVE-2026-33745** (1-1.5 天)

**理由**:
- Threading fix 已隐式完成,无需升级
- **CVE-2026-33745 是 Critical security 风险**,即使当前未触发,未来引入 follow_location 风险高
- 顺手升级 httplib 单文件 ~10000+ 行,merge conflict 风险评估 (per ADR-0087 §风险 §Step 2)

**升级目标版本**:
- 最低: v0.39.0 (含 CVE fix)
- 推荐: v0.39.x 最新 patch (例如 v0.39.2 if exists)
- 避免: v0.40+ (可能引入新 break points,需评估)

### 5.3 Sprint 26+ 真实根因重诊断

**当前未解之谜**: SerializingDecorator 仍有效 (8 worker × 20 task = 160 calls 零 SIGSEGV),但 PR #701 + ctx_mutex + request_mutex + authorization_count_ 已全部就位。

**可能解释**:
1. **历史 httplib 版本** (PR #701 之前) 的 bug,升级到 0.18.4 时未保留 SerializingDecorator,但生产仍稳定 (因为 PR #701 fix 也在 0.18.4) → 根因已**自然修复**,SerializingDecorator 成为**过度保护**
2. **httplib 内部更细的 race** (chunked transfer / stream 解析 / SSE decoder) — 即使 0.18.4 仍有
3. **旧 OpenSSL 1.1.1 行为** (当前系统已 3.0.13) — 已无影响

**Sprint 26 实证方案**:
1. 升级到 httplib 0.39.0+ (含 CVE fix)
2. 暂时移除 SerializingDecorator 默认包装 (factory cloud 路径)
3. 跑 8 worker × 20 task stress (test_cloud_adapter_multithread.cpp)
4. 跑 Phase E Skill IPC + Phase G ContextCompactor (多 worker 真并发)
5. **如果零 SIGSEGV** → ADR-0087 §Decision 1 "默认包装" 可移除,SerializingDecorator 真正 OPT-IN
6. **如果仍 SIGSEGV** → httplib 0.39.0 后根因更细,需 gdb 重诊断 (类似 c0cb522 Phase B 流程)

---

## 6. 风险与缓解

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| httplib 0.18.4 → 0.39.0 merge conflict 高 (单文件 ~10000+ 行) | 高 | 中 | 预留半天,优先 conflict 解决 + Phase 0 unit 测试 |
| 新 httplib API 变化 (set_follow_location / new Stream API) 破坏现有测试 | 中 | 高 | Phase 0 unit + Phase B mock test 守门 |
| 升级后真根因仍未消除,Sprint 26 重诊断成本上升 | 中 | 中 | 本审计已明确"实证方案",可立即执行 |
| 下游消费者 (hydraforge-pdk dual-repo) 仍用 0.18.4 | 低 | 低 | ADR §兼容性章节记录升级要求 + sync script 自动化 |

---

## 7. 参考资料

### 7.1 httplib issue/PR

- **PR #701** (merged Nov 29, 2020): https://github.com/yhirose/cpp-httplib/pull/701
- Issue #697 (Windows SSL_shutdown): https://github.com/yhirose/cpp-httplib/issues/697
- Issue #699 (reopen #697): https://github.com/yhirose/cpp-httplib/issues/699
- **CVE-2026-33745** (GHSA-6hrp-7fq9-3qv2): https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-6hrp-7fq9-3qv2
- Issue #1903 (Windows file size, 与 threading 无关): https://github.com/yhirose/cpp-httplib/issues/1903

### 7.2 httplib Authorization 相关 issues (排除)

- Issue #466 (Digest auth WWW-Authorization vs Authorization) — Bug,fixed
- Issue #484 (Bearer token feature) — Enhancement
- Issue #127 (Basic auth support) — Shipped
- **GHSA-6hrp-7fq9-3qv2** (Critical security) — Fixed in v0.39.0

### 7.3 项目内部引用

- ADR-0087 §根因诊断: `docs/adr/adr-0087-cloud-adapter-threading-model.md` line 54-84
- ADR-0087 §Step 2: `docs/adr/adr-0087-cloud-adapter-threading-model.md` line 100-108
- SerializingDecorator 部署位置: `src/common/llm/llm_provider_factory.cpp:102-114`
- c0cb522 Phase B gdb 实证: `git show c0cb522`
- vendored httplib 关键代码: `external/cpp-httplib/httplib.h:11, 663, 1507-1509, 7477, 8940-8968`
- 关联审计: `docs/audits/2026-09-10-adr-0087-sprint-24-openssl-audit.md`

---

**调研结论**: ADR-0087 §Step 2 假设不成立 (PR #701 fix 已在 0.18.4)。**真正升级理由是 CVE-2026-33745 (Critical security)**,目标版本 **v0.39.0+**。建议 Sprint 25 重定位至 security 升级,Sprint 26 通过实证方案判断 SerializingDecorator 是否仍必要。
