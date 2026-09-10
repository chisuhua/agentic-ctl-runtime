# upgrade-httplib-0541 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use skill_use("execute") to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 vendored cpp-httplib 从 0.18.4 升级到 v0.54.1,覆盖 4 个 client 侧 security advisories (CVE-2026-33745 High / GHSA-39q5-hh6x-jpxx High / GHSA-h6wq-j5mv-f3q8 Moderate / GHSA-c3h8-fqq4-xm4g High-conditional),回归全部 6 个 httplib 使用点。

**Architecture:** 单头文件替换策略 — vendored `external/cpp-httplib/httplib.h` 整体替换为 upstream v0.54.1,前置 SHA256 验证零本地 patch,以"版本守卫测试" (TDD 红→绿) 驱动替换,替换后全量 ctest 回归 + `set_follow_location` grep 守卫防 CVE 未来触发。

**Tech Stack:** C++20 + Catch2 (v3.7.0+) + CMake 3.20+ + curl (下载 upstream httplib.h) + OpenSSL 3.0.13 (系统已有)。

---

## File Structure

### Production Code (修改)

| File | Responsibility |
|---|---|
| `external/cpp-httplib/httplib.h` | vendored 单文件,0.18.4 → v0.54.1 整体替换 |
| `src/common/llm/cloud_adapter.cpp` | 仅编译验证 (若 v0.52.0 Headers ABI break 需适配 L250) |
| `src/common/llm/http_adapter.cpp` | 仅编译验证 (若 Headers ABI break 需适配 L172) |
| `src/common/env/docker_backend.cpp` | 仅编译验证 (Unix socket Client) |

### Tests (新建/修改)

| File | Responsibility |
|---|---|
| `tests/test_httplib_version.cpp` | 新建 — 版本守卫测试 (CPPHTTPLIB_VERSION == "0.54.1"),TDD 红→绿驱动 |
| `tests/test_http_adapter.cpp` | 回归 (7 cases, mock Server) |
| `tests/test_docker_backend.cpp` | 回归 (Server) |
| `tests/test_cloud_adapter_multithread.cpp` | 回归 (8 worker stress, HTTPS+Authorization) |
| `tests/test_serializing_decorator.cpp` | 回归 (4 unit, 确保包装不回归) |

### Scripts (新建)

| File | Responsibility |
|---|---|
| `scripts/check-httplib-no-follow-location.sh` | CVE-2026-33745 守卫: grep set_follow_location 全项目 0 使用 (排除 external/) |

### Documentation (修改)

| File | Responsibility |
|---|---|
| `docs/adr/adr-0087-cloud-adapter-threading-model.md` | §实施日志追加 Sprint 25 升级记录 (v0.54.1, SHA256, 4/4 advisories) |
| `openspec/changes/upgrade-httplib-0541/tasks.md` | 执行进度勾选 (execute 阶段更新) |

---

## Task 1: 版本守卫测试 (TDD 红)

**Files:**
- Create: `tests/test_httplib_version.cpp`
- Test: 同上 (版本断言)

- [ ] **Step 1: 新建 tests/test_httplib_version.cpp**

在 `tests/test_httplib_version.cpp` 写入 (与既有测试同风格, 中文注释):

```cpp
// tests/test_httplib_version.cpp
// 功能描述: vendored cpp-httplib 版本守卫 — 确保升级到覆盖 client 侧
//           security advisories 的版本 (v0.54.1)
// 设计依据: openspec/changes/upgrade-httplib-0541/design.md D1
// 作者: AgenticDSL Sprint 25
// 最后修改日期: 2026-09-10

#include "catch_amalgamated.hpp"

#include <httplib.h>
#include <string>

using namespace agenticdsl;

TEST_CASE("vendored cpp-httplib SHALL be v0.54.1 (security advisories covered)",
          "[httplib][security]") {
  // 4 个 client 侧 advisories 覆盖要求:
  // - CVE-2026-33745 (High 7.4)   → fixed in v0.39.0
  // - GHSA-39q5-hh6x-jpxx (High)  → fixed in v0.5x
  // - GHSA-h6wq-j5mv-f3q8 (Mod)   → fixed in v0.5x
  // - GHSA-c3h8-fqq4-xm4g (High)  → fixed in v0.5x
  // v0.54.1 (latest stable 2026-08-30) 一次性覆盖 4/4.
  const std::string version = CPPHTTPLIB_VERSION;
  REQUIRE(version == "0.54.1");
}

TEST_CASE("httplib Headers SHALL be iterable-constructible (cloud_adapter L250)",
          "[httplib][security]") {
  // v0.52.0 将 Headers 改为 insertion-ordered multimap,
  // value_type 从 pair<const string, Mapped> → pair<string, Mapped>.
  // 回归: cloud_adapter.cpp:250 与 http_adapter.cpp:172 的迭代器构造必须仍可用.
  std::vector<std::pair<std::string, std::string>> vec;
  vec.emplace_back("Content-Type", "application/json");
  vec.emplace_back("Authorization", "Bearer test-key");
  httplib::Headers headers(vec.begin(), vec.end());
  REQUIRE(headers.count("Content-Type") == 1);
  REQUIRE(headers.count("Authorization") == 1);
  REQUIRE(headers.find("Content-Type")->second == "application/json");
}
```

- [ ] **Step 2: 运行测试验证失败**

Run: `cmake --preset tests -DAGENTICDSL_BUILD_TESTS=ON && cmake --build build -j$(nproc) && ctest --test-dir build -R test_httplib_version`
Expected: FAIL — `REQUIRE(version == "0.54.1")` 失败,因当前 `CPPHTTPLIB_VERSION == "0.18.4"`。
注: `tests/CMakeLists.txt` 用 `file(GLOB test_*.cpp)` 自动注册,**必须先 cmake configure** 才会捕获新测试 (AGENTS.md §测试模式 4)。

- [ ] **Step 3: (本 Task 无实现 — 替换在 Task 2)**

本 Task 是测试前置。Step 4 验证 red 状态保留即可。

- [ ] **Step 4: 记录 red 状态**

Run: 同 Step 2。Expected: 确认 `test_httplib_version` FAIL (版本不匹配),证明测试确实在拦截旧版本。

- [ ] **Step 5: Defer commit**

按仓库约定,execute 阶段不逐任务 commit;所有变更将在 archive 阶段统一提交。

---

## Task 2: 替换 vendored httplib.h → v0.54.1 (TDD 绿)

**Files:**
- Modify: `external/cpp-httplib/httplib.h` (整体替换)

- [ ] **Step 1: M3 前置验证 + 备份 + 下载替换**

```bash
# (a) M3: 验证 vendored 零本地 patch (SHA256 vs upstream v0.18.4)
#     若不一致, 需 diff 确认是何种本地修改, 记录后再继续
curl -sL https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.4/httplib.h | sha256sum
# 期望 = ca2fc115558d72942b3235afb35759a7056146cc55cf38063ea5645517024066
# (b) 备份
cp external/cpp-httplib/httplib.h /tmp/opencode/httplib-v0.18.4.h.bak
# (c) 下载 v0.54.1 替换
curl -sL https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.54.1/httplib.h \
  -o external/cpp-httplib/httplib.h
# (d) 记录新 SHA256
sha256sum external/cpp-httplib/httplib.h
```

- [ ] **Step 2: 编译全量 (含新版本守卫测试)**

Run: `cmake --preset tests -DAGENTICDSL_BUILD_TESTS=ON && cmake --build build -j$(nproc)`
Expected: exit 0。若 `httplib::Headers(vec.begin(), vec.end())` 编译失败 (D3 预判),执行适配:
```cpp
// cloud_adapter.cpp:250 / http_adapter.cpp:172 改为显式插入
httplib::Headers headers;
for (const auto& [k, v] : headers_vec) headers.emplace(k, v);
```

- [ ] **Step 3: 运行版本守卫测试验证通过**

Run: `ctest --test-dir build -R test_httplib_version`
Expected: PASS — `CPPHTTPLIB_VERSION == "0.54.1"` 且 Headers 迭代器构造可用。

- [ ] **Step 4: 专项回归 (使用点)**

Run: `ctest --test-dir build -R "test_http_adapter|test_docker_backend|test_serializing_decorator"`
Expected: 全部 PASS (http_adapter 7 cases + docker + serializing 4 unit)。

- [ ] **Step 5: Defer commit**

同 Task 1。

---

## Task 3: 全量 ctest 回归 + set_follow_location 守卫

**Files:**
- Create: `scripts/check-httplib-no-follow-location.sh`
- Test: 无新测试 (守卫脚本自带实证)

- [ ] **Step 1: 新建守卫脚本 scripts/check-httplib-no-follow-location.sh**

```bash
#!/usr/bin/env bash
# check-httplib-no-follow-location.sh
# 功能: CVE-2026-33745 守卫 — 禁止项目代码调用 set_follow_location
#       (Authorization + follow_location + cross-origin redirect → 凭证泄露)
# 用法: ./scripts/check-httplib-no-follow-location.sh  (exit 0 = 通过)
set -euo pipefail
cd "$(dirname "$0")/.."
# 排除 external/ (vendored httplib.h 自身含 API 声明, 非使用)
MATCHES=$(grep -rn "set_follow_location" src/ pdk/ examples/ tests/ 2>/dev/null || true)
if [ -n "$MATCHES" ]; then
  echo "ERROR: set_follow_location 被使用 (CVE-2026-33745 风险):"
  echo "$MATCHES"
  exit 1
fi
echo "OK: set_follow_location 全项目 0 使用 (排除 external/)"
```

- [ ] **Step 2: 运行守卫脚本实证**

Run: `chmod +x scripts/check-httplib-no-follow-location.sh && ./scripts/check-httplib-no-follow-location.sh`
Expected: exit 0, 输出 "OK: set_follow_location 全项目 0 使用"。
注: 当前实证已确认 src/pdk/examples/tests 0 匹配 (Oracle L2 验证)。

- [ ] **Step 3: 全量 ctest 回归**

Run: `HYDRAFORGE_SKIP_REAL_LLM=1 ctest --test-dir build -j$(nproc)`
Expected: **228/228 PASS 零回归** (228 baseline + 新 test_httplib_version 2 cases)。

- [ ] **Step 4: ASan 快速验证 (可选但推荐)**

Run: `cmake --preset asan -DAGENTICDSL_BUILD_TESTS=ON && cmake --build build-asan -j$(nproc) && ctest --test-dir build-asan -R "test_http_adapter|test_docker_backend|test_httplib_version"`
Expected: PASS (依赖升级后无新 memory issue)。

- [ ] **Step 5: Defer commit**

同 Task 1。

---

## Task 4: 文档 + 架构合规性验证

**Files:**
- Modify: `docs/adr/adr-0087-cloud-adapter-threading-model.md` (§实施日志追加)
- Modify: `openspec/changes/upgrade-httplib-0541/tasks.md` (勾选)

- [ ] **Step 1: ADR-0087 §实施日志追加 Sprint 25 记录**

在 `docs/adr/adr-0087-cloud-adapter-threading-model.md` 的 §实施日志 后追加:

```markdown
### Sprint 25 实施 (2026-09-10) — httplib v0.54.1 升级

**实施载体**: OpenSpec change `upgrade-httplib-0541`
**结果**: vendored cpp-httplib 0.18.4 → v0.54.1 升级完成, 4/4 client 侧 advisories 覆盖
- SHA256 (v0.54.1): [填入实际值]
- 回归: 全量 ctest 228/228 PASS (含新 test_httplib_version 2 cases)
- 守卫: scripts/check-httplib-no-follow-location.sh (CVE-2026-33745)
- 6 使用点验证: cloud_adapter / http_adapter / docker_backend / http_mock_server / test_http_adapter / test_docker_backend
- SerializingDecorator 默认包装**保持** (Sprint 27 再移除)
```

- [ ] **Step 2: openspec validate + adr_lint + docs_drift_audit**

Run:
```bash
openspec validate --strict
python3 tools/adr_lint.py        # 期望 68 ADR PASS (0 errors, 2 pre-existing WARNING 不变)
python3 tools/docs_drift_audit.py  # 期望 0 DRIFT
./scripts/check-lsp-discipline.sh --quick  # 期望 PASS
```

- [ ] **Step 3: 更新 openspec/changes/upgrade-httplib-0541/tasks.md**

将 tasks.md 中已完成的 `- [ ]` 改为 `- [x]` (Task 1.1~6.4),保留 7.x 待 Oracle 复核。

- [ ] **Step 4: (无实现 — 文档任务)**

- [ ] **Step 5: Defer commit**

同 Task 1。

---

## Task 5: Oracle 复核 + archive 收尾

**Files:**
- Modify: `openspec/changes/upgrade-httplib-0541/tasks.md` (Task 7.x 勾选)
- 归档: `openspec/changes/archive/2026-09-10-upgrade-httplib-0541/`

- [ ] **Step 1: 派 Oracle 复核 (SHIP-gate)**

Run: 派 Oracle 审核本 change (续 Sprint 24 session `ses_f760c60d5ffe9hmgREbWz0hA8u` 或新开),输入:
- 替换后 `external/cpp-httplib/httplib.h` 的 SHA256
- `test_httplib_version.cpp` 内容
- `scripts/check-httplib-no-follow-location.sh` 内容
- 全量 ctest 输出 (228/228)
- ADR-0087 §实施日志 Sprint 25 段落

Expected: Oracle 返回 SHIP / SHIP-with-fixes (修正后重验)。

- [ ] **Step 2: 应用 Oracle fixes (若有) + atomic commits**

```bash
# Commit 1 (依赖升级): vendored httplib.h + test_httplib_version.cpp + 守卫脚本
# Commit 2 (文档): ADR-0087 §实施日志 + tasks.md 勾选
# (若 Headers 适配, 并入 Commit 1)
```

- [ ] **Step 3: OpenSpec archive**

Run: `openspec archive upgrade-httplib-0541` + 更新 `docs/active-status.md`。

- [ ] **Step 4: ADR-0087 状态评估**

Sprint 25 升级完成后,评估是否满足 ADR-0087 §升级触发条件 (httplib 明确多线程 fix 已落地) → 决定 Sprint 26 实证方案启动时机。

- [ ] **Step 5: Defer commit (archive 时统一)**

---

## Self-Review

**1. Spec 覆盖检查**:
- ✅ `httplib-client-security` R1 (v0.54.1) → Task 1+2
- ✅ `httplib-client-security` R2 (6 使用点回归) → Task 2.4 + Task 3.3
- ✅ `httplib-client-security` R3 (set_follow_location 守卫) → Task 3.1-3.2
- ✅ design D1/D2/D3/D4/D5 全部落 Task
- ✅ M3 前置 (vendored 零 patch) → Task 2 Step 1(a)

**2. 占位符扫描**: 无 "TBD"/"TODO"/"implement later" — 每步含实际命令与代码。

**3. 类型一致性**: `CPPHTTPLIB_VERSION` 宏 / `httplib::Headers` / `httplib::Client` 命名跨 Task 一致;守卫脚本路径 `scripts/check-httplib-no-follow-location.sh` 在 Task 3/4/5 引用一致。
