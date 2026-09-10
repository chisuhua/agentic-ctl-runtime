# Tasks: Upgrade vendored cpp-httplib 0.18.4 → v0.54.1

## 1. 前置验证 (M3: vendored 零本地 patch)

- [ ] 1.1 diff vendored `external/cpp-httplib/httplib.h` vs upstream v0.18.4 tag (SHA256 `ca2fc115558d72942b3235afb35759a7056146cc55cf38063ea5645517024066` vs `curl -L https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.4/httplib.h | sha256sum`) → 确认零本地 patch
- [ ] 1.2 备份 vendored 到 `/tmp/opencode/httplib-v0.18.4.h.bak`

## 2. 依赖升级 (D1: v0.54.1 替换)

- [ ] 2.1 从 GitHub releases 确认 v0.54.1 为 latest stable (2026-08-30),获取 `https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.54.1/httplib.h` 替换 vendored
- [ ] 2.2 记录新 vendored SHA256 到 design/ADR-0087 (变更后入库)

## 3. 编译适配 (D3: v0.52.0 Headers ABI break 检查)

- [ ] 3.1 编译 `cloud_adapter.cpp` + `http_adapter.cpp`,验证 `httplib::Headers(vec.begin(), vec.end())` 迭代器构造在 v0.54.1 下兼容
- [ ] 3.2 若编译失败 → 改为等价插入式构造 (`headers.emplace(kv.first, kv.second)`),并重跑 3.1
- [ ] 3.3 编译 `docker_backend.cpp` (Unix socket Client,执行 set_connection_timeout/set_read_timeout) 确认无 API 变化

## 4. 回归测试 (D4: 全量 ctest + 使用点专项)

- [ ] 4.1 构建: `cmake --preset debug -DAGENTICDSL_BUILD_TESTS=ON && cmake --build build -j$(nproc)` exit 0
- [ ] 4.2 专项: `ctest --test-dir build -R "test_http_adapter|test_docker_backend|test_serializing_decorator"` 全部 PASS
- [ ] 4.3 全量: `HYDRAFORGE_SKIP_REAL_LLM=1 ctest --test-dir build -j$(nproc)` → **228/228 PASS 零回归**

## 5. 安全守卫 (D5/L2: set_follow_location grep)

- [ ] 5.1 新增 grep 守卫脚本 (或复用 scripts/ 模式): `grep -rn "set_follow_location" src/ pdk/ examples/ tests/` 必须返回 0 (排除 external/)
- [ ] 5.2 守卫脚本注册到 `scripts/check-lsp-discipline.sh` 或 CI 工作流 (`.github/workflows/ci.yml`)
- [ ] 5.3 实证: 运行守卫脚本 → 0 匹配 (当前全项目 set_follow_location 0 使用)

## 6. 文档与架构合规性验证

- [ ] 6.1 ADR-0087 §实施日志追加 Sprint 25 升级记录: 目标 v0.54.1, 4/4 advisories 覆盖, 6 使用点回归, SHA256, set_follow_location 守卫
- [ ] 6.2 `openspec validate --strict` exit 0 (含本 change proposal/design/spec/tasks 完整性)
- [ ] 6.3 `python3 tools/adr_lint.py` 68 ADR PASS + `python3 tools/docs_drift_audit.py` 0 DRIFT
- [ ] 6.4 LSP discipline: `./scripts/check-lsp-discipline.sh --quick` PASS (0 real errors)

## 7. 收尾 (SHIP-with-fixes 流程 per AGENTS.md §4)

- [ ] 7.1 atomic commit: 替换 vendored httplib.h + 编译适配 + grep 守卫脚本 (1 commit)
- [ ] 7.2 atomic commit: 文档 + ADR-0087 §实施日志 + SHA256 记录 (1 commit)
- [ ] 7.3 派 Oracle 复核 (续 Sprint 24 session 或新开): 拿 SHIP/APPROVE 才 archive
- [ ] 7.4 OpenSpec archive + active-status.md 更新 + AGENTS.md 模式沉淀 (若新发现)