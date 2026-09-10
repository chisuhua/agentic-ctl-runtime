# httplib-client-security Specification

Vendored cpp-httplib 升级至覆盖全部 client 侧 security advisories 的 release,回归全部 httplib 使用点,并禁止未来引入 `set_follow_location(true)` 于含 Authorization 请求路径 (CVE-2026-33745 守卫)。

## Requirements

### Requirement: vendored cpp-httplib SHALL 升级至 v0.54.1

`external/cpp-httplib/httplib.h` SHALL 升级至 **v0.54.1** (latest stable, 2026-08-30)。升级后 MUST 覆盖以下 4 个 client 侧 security advisories: CVE-2026-33745 (High 7.4), GHSA-39q5-hh6x-jpxx (High), GHSA-h6wq-j5mv-f3q8 (Moderate), GHSA-c3h8-fqq4-xm4g (High-conditional)。升级过程 MUST 记录新版本 SHA256。

#### Scenario: httplib.h 版本已升级
- GIVEN verdored `external/cpp-httplib/httplib.h`
- WHEN 读取 `CPPHTTPLIB_VERSION` 宏
- THEN 其值等于 `"0.54.1"`
- AND 升级记录的 SHA256 与文件实际 SHA256 一致

#### Scenario: 覆盖全部 client 侧 advisories
- GIVEN 升级后的 vendored httplib
- WHEN 对比 httplib security advisories 列表
- THEN 版本 >= 覆盖 CVE-2026-33745 + GHSA-39q5 + GHSA-h6wq + GHSA-c3h8 的修复版本

### Requirement: 全部 httplib 使用点 SHALL 回归

升级后 SHALL 回归全部 6 个 httplib 使用点 (3 生产 + 3 测试),全量 ctest 零回归 (228 baseline)。

#### Scenario: cloud_adapter HTTPS + Authorization 回归
- GIVEN `src/common/llm/cloud_adapter.cpp` (v0.54.1 下编译)
- WHEN 运行 `test_cloud_adapter_multithread` (8 worker stress, HTTPS + Authorization)
- THEN 编译通过
- AND SerializingDecorator 默认包装保持 (行为不变)

#### Scenario: http_adapter + docker_backend 回归
- GIVEN `src/common/llm/http_adapter.cpp` + `src/common/env/docker_backend.cpp` (v0.54.1 下编译)
- WHEN 运行 `test_http_adapter` + `test_docker_backend`
- THEN 全部测试 PASS (7 cases http + 既有 docker cases)

#### Scenario: Headers 迭代器构造兼容
- GIVEN `httplib::Headers(headers_vec.begin(), headers_vec.end())` 调用点 (cloud_adapter L250, http_adapter L172)
- WHEN v0.52.0+ (insertion-ordered multimap) 下编译
- THEN 编译成功 (或等价插入式构造替代)
- AND 运行时 header 传递正确

#### Scenario: 全量 ctest 零回归
- GIVEN 升级完成
- WHEN `HYDRAFORGE_SKIP_REAL_LLM=1 ctest --test-dir build -j$(nproc)`
- THEN 228/228 PASS
- AND `openspec validate --strict` exit 0
- AND `python3 tools/adr_lint.py` PASS
- AND `python3 tools/docs_drift_audit.py` 0 DRIFT

### Requirement: set_follow_location SHALL 保持禁用

项目代码 SHALL 永不调用 `httplib::Client::set_follow_location(true)` 于含 Authorization 请求路径 (CVE-2026-33745 触发条件: follow_location + Authorization header + cross-origin redirect → 凭证泄露给第三方)。CI grep 守卫 SHALL 强制 `set_follow_location` 在 `src/` + `pdk/` + `examples/` + `tests/` 零使用 (排除 `external/`)。

#### Scenario: set_follow_location 全项目零使用
- GIVEN CI grep 守卫命令 `grep -rn "set_follow_location" src/ pdk/ examples/ tests/`
- WHEN 运行
- THEN 返回 0 匹配 (排除 `external/`)
- AND 输出为空 (无 Authorization 泄露路径)

#### Scenario: 注释方式保留告警
- GIVEN 开发者在 cloud_adapter/http_adapter 中尝试添加 follow_location
- WHEN grep 守卫运行
- THEN 检查失败 (CI 红)
- AND 需在 ADR-0087 §实施日志说明为何无害 (例如: 已升级 v0.54.1 覆盖 CVE 或确认无 Authorization)