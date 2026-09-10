# Design: Upgrade vendored cpp-httplib 0.18.4 → v0.54.1

## Context

- vendored `external/cpp-httplib/httplib.h` 固定 **0.18.4** (单文件 ~10.4k 行,非 git submodule,SHA256 `ca2fc115558d72942b3235afb35759a7056146cc55cf38063ea5645517024066`)
- Sprint 24 调研 (commit `0a76de0`/`26749c2`/`3ef9bb2`) + Oracle 评审 (`ses_f760c60d5ffe9hmgREbWz0hA8u`) 确认:
  - **4 个 client 侧 security advisories 未覆盖**: CVE-2026-33745 (High 7.4) + GHSA-39q5-hh6x-jpxx (High, Content-Length crash) + GHSA-h6wq-j5mv-f3q8 (Moderate, 负 chunk-size DoS) + GHSA-c3h8-fqq4-xm4g (High-conditional, proxy TLS bypass)
  - v0.39.0 仅覆盖 1/4 (CVE-2026-33745) → 目标 **v0.54.1** (latest stable, 2026-08-30)
  - 系统 OpenSSL 已 3.0.13 + cloud_adapter.cpp 0 行 OpenSSL 代码 → OpenSSL 集成非本 change 范围 (0 LOC)
- 本 change 是 ADR-0087 §Step 2 的实施载体 (httplib 升级),ADRD-0087 已翻牌 🟡 Partial
- 约束: 项目 (HydraForge) 为 C++20 / CMake 3.20+,2 空格缩进,中文注释 (AGENTS.md)

## Goals / Non-Goals

**Goals:**
- 替换 vendored httplib 0.18.4 → v0.54.1,覆盖 4/4 client 侧 security advisories
- 回归 6 个 httplib 使用点 (3 生产 + 3 测试),全量 ctest 零回归 (228 baseline)
- 新增 `set_follow_location` CI grep 守卫 (CVE-2026-33745 未来触发路径防护,Oracle L2)
- 验证 vendored 零本地 patch (Oracle M3 前置)

**Non-Goals:**
- **不**移除 SerializingDecorator 默认包装 (`llm_provider_factory.cpp:102-114`) — ADR-0087 §Step 4 / Sprint 27
- **不**做 OpenSSL 3.0 集成 (0 LOC,已隐式完成 — `docs/audits/2026-09-10-adr-0087-sprint-24-openssl-audit.md`)
- **不**做真根因 gdb 重诊断 (`create_client_socket` SIGSEGV) — Sprint 26
- **不**做 4 worker benchmark — Sprint 28

## Decisions

### D1: 升级目标 = v0.54.1 (latest stable)

**决策**: 升级至 **v0.54.1** (2026-08-30),非 v0.39.0。

**理由**:
- v0.39.0 仅覆盖 CVE-2026-33745 (1/4 advisories)
- GHSA-39q5 (High) + GHSA-h6wq (Moderate) 需 v0.5x 系列修复 — **当前直接暴露** (恶意 LLM 端点可崩 client / chunked DoS)
- v0.54.1 一次性覆盖 4/4 advisories + 21 个 minor 版本累积 bug fixes

**Alternatives**: 
- v0.39.x (最小 security 升级) → 拒绝: GHSA-39q5/h6wq 未覆盖
- v0.4x/v0.5x 中间版本 → 拒绝: 无必要,直接 latest 减少再次升级

### D2: vendored 单文件替换 (非 submodule 迁移)

**决策**: 保持 vendored 模式,直接替换 `external/cpp-httplib/httplib.h`。

**理由**:
- httplib 非 git submodule (GitHub CI 也不依赖 submodule init)
- 转换为 submodule 引入 CI 复杂度,无收益
- 保持与最终驱逐方案 (httplib 上游 fork 或移除) 一致

**前置 (M3)**: diff vendored httplib.h vs upstream v0.18.4 tag 验证零本地 patch (SHA256 对比或 git diff)

### D3: Headers 构造 v0.52.0 ABI break 兼容策略

**决策**: 先用现有 `httplib::Headers(vec.begin(), vec.end())` 编译验证;若 v0.52.0+ 迭代器构造不兼容,改 `for (auto& kv : vec) headers.insert(kv);` 或 `headers.emplace(kv.first, kv.second);`。

**理由**: 
- v0.52.0 将 `Headers` 从 `std::multimap` 改为 insertion-ordered,value_type 从 `pair<const string, Mapped>` → `pair<string, Mapped>` 可能影响迭代器构造
- 本项目只用 2 处迭代器构造 (cloud_adapter L250, http_adapter L172),影响面小

**Alternatives**:
- 直接插入式构造 (不依赖迭代器) → 若编译失败启用

### D4: 回归策略 = 全量 ctest + 3 使用点专项

**决策**: 升级后跑全量 ctest (228 baseline) + 专项验证:
- `test_cloud_adapter_multithread` (HTTPS + Authorization, 8 worker stress)
- `test_http_adapter` (HTTP Server mock, 7 cases)
- `test_docker_backend` (Server)
- `test_serializing_decorator` (4 unit) — 确保包装不回归

**理由**: 复用 AGENTS.md §ENGINEERING PATTERNS #4 SHIP-with-fixes 流程 + real-llm 测试模式

### D5: set_follow_location CI grep 守卫 (Oracle L2)

**决策**: 新增 grep 守卫脚本检查 `set_follow_location` 全项目 0 使用 (`src/` + `pdk/` + `examples/` + `tests/`,排除 `external/`)。

**理由**: 防未来引入 CVE-2026-33745 触发路径 (Authorization + follow_location → 凭证泄露给第三方)

## Risks / Trade-offs

| 风险 | 缓解 |
|------|------|
| [跨 36 minor 版本 API/行为漂移] → 编译错误或运行时行为变化 | D3 兼容策略 + 全量 ctest (228) + LSP discipline |
| [vendored 有未记录本地 patch] → 升级丢失 patch | D2 M3 前置 diff 验证零本地修改 |
| [GHSA-39q5 触发场景新增] → 恶意 LLM 端点崩 client | v0.54.1 修复后无此风险;专项目标测试可选 |
| [升级后真根因未消除] → Sprint 26 实证失败 | SerializingDecorator 默认包装**保持** (本 change 不动),Decision 3 OPT-IN 可逆 |
| [v0.54.1 引入新 CVE (发布后)] → 本升级未覆盖 | 订阅 httplib security advisories;升级后定期复查 |

## Migration Plan

1. **M3 前置**: diff vendored httplib.h vs upstream v0.18.4 tag (SHA256 对比) → 确认零本地 patch
2. **备份**: `cp external/cpp-httplib/httplib.h /tmp/opencode/httplib-v0.18.4.h.bak`
3. **下载替换**: 获取 v0.54.1 httplib.h → 覆盖 vendored
4. **记录 SHA256**: 新版本 SHA256 写入 ADR-0087 §实施日志 + design
5. **编译验证**: build 4 个生产/测试使用点 (D3 兼容策略)
6. **全量 ctest**: `HYDRAFORGE_SKIP_REAL_LLM=1 ctest -j$(nproc)` → 228/228
7. **后续**: `openspec validate --strict` + `adr_lint` + `docs_drift_audit`
8. **回滚策略**: `/tmp/opencode/httplib-v0.18.4.h.bak` + git revert (vendored 单文件,回滚成本极低)

## Open Questions

- v0.54.1 是否确为 2026-08-30 latest stable?实施时从 GitHub releases 确认
- `httplib::Headers` 迭代器构造在 v0.52.0+ 是否兼容?编译期实证
- vendored vs upstream v0.18.4 是否有本地 patch 漂移 (M3)?实施时 SHA256/diff 实证
- 是否需要将 httplib 使用点从 `#include <httplib.h>` (系统/include path) 改为相对路径 (vendored)?当前 include path 通过 CMake target_include_directories 注入,不涉及

## 架构合规性检查

- **本 change 遵循**: ADR-0087 §Step 2 (httplib 升级) — 是 ADR-0087 升级路径的实施载体,ADR 已翻牌 🟡 Partial
- **不违反现有 ADR**: 不改变 SerializingDecorator 默认包装 (ADR-0087 §决策 1 保持),不改变 CloudLLMAdapter 行为 (httplib 内部实现细节)
- **与 docs/specs/architecture.md 一致**: L0 运行时 HTTP 层依赖升级,不改变架构分层
- **无新 ADR 需求**: 本 change 是既有决策的实施,不引入新架构决策点