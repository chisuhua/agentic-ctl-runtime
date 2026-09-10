# Proposal: Upgrade vendored cpp-httplib 0.18.4 → v0.54.1

## Why

vendored `external/cpp-httplib/httplib.h` 固定为 0.18.4,存在 **4 个未覆盖的 client 侧 security advisories** (CVE-2026-33745 High + GHSA-39q5-hh6x-jpxx High + GHSA-h6wq-j5mv-f3q8 Moderate + GHSA-c3h8-fqq4-xm4g High-conditional)。其中 GHSA-39q5 (恶意 Content-Length 响应头 → client 进程崩溃) 与 GHSA-h6wq (负 chunk-size DoS,LLM streaming 走 chunked transfer 直接暴露) **当前即直接暴露**,非仅未来风险。Sprint 24 调研 (commit `0a76de0` / `26749c2` / `3ef9bb2`) 实证:系统 OpenSSL 已 3.0.13 + cloud_adapter.cpp 0 行 OpenSSL 代码 + PR #701 threading fix 已在 0.18.4 — 因此**升级 httplib 至 v0.54.1 是本 ADR-0087 §Step 2 的最高价值动作** (覆盖 4/4 advisories + 21 个 minor 版本累积 bug fixes)。

## What Changes

- **BREAKING** (依赖升级): 替换 `external/cpp-httplib/httplib.h` 0.18.4 → **v0.54.1** (latest stable, 2026-08-30; SHA256 入库记录)
- 回归验证 6 个 httplib 使用点: `cloud_adapter.cpp` + `http_adapter.cpp` + `docker_backend.cpp` + `http_mock_server.h` + `test_http_adapter.cpp` + `test_docker_backend.cpp`
- 编译适配 v0.52.0 `Headers` insertion-ordered multimap ABI break (若 `httplib::Headers(vec.begin(), vec.end())` 迭代器构造不兼容)
- 新增 `set_follow_location` CI grep 守卫 (防未来引入 CVE-2026-33745 触发路径)
- **不**移除 SerializingDecorator 默认包装 (该决策属 ADR-0087 §Step 4 / Sprint 27,不在本 change)

## Capabilities

### New Capabilities
- `httplib-client-security`: vendored cpp-httplib 升级至覆盖全部 client 侧 security advisories 的 release;回归全部 httplib 使用点;禁止未来引入 `set_follow_location(true)` 于含 Authorization 请求路径 (CVE-2026-33745 守卫)

### Modified Capabilities
- (无 — 既有 `fix-cloud-adapter-multithreading` (SerializingDecorator) 与 `cloud-adapter-threading` (ADR-0087 scaffold) 的 requirement 均不因本升级改变;它们描述的行为 (串行化包装 + OPT-IN) 在本 change 保持不变)

## Impact

- **依赖**: `external/cpp-httplib/httplib.h` (vendored 单文件,非 submodule; ~10.4k 行)
- **受影响代码 (6 文件)**:
  - `src/common/llm/cloud_adapter.cpp` (HTTPS + Authorization Client, L15/244/250)
  - `src/common/llm/http_adapter.cpp` (HTTP Client, L10/166/172)
  - `src/common/env/docker_backend.cpp` (Docker over Unix socket Client, L12/28-34)
  - `tests/test_helpers/http_mock_server.h` (Server, L8/57-60)
  - `tests/test_http_adapter.cpp` (7 cases, Server)
  - `tests/test_docker_backend.cpp` (Server)
- **测试**: `test_http_adapter` + `test_docker_backend` + 全量 ctest (228 baseline) + ASan/TSan 可选
- **安全**: 修复 4/4 client 侧 advisories
- **Non-goals**: OpenSSL 集成 (0 LOC,已隐式完成 — 见 `docs/audits/2026-09-10-adr-0087-sprint-24-openssl-audit.md`);移除 SerializingDecorator 默认包装 (Sprint 27);真根因 gdb 重诊断 (Sprint 26)