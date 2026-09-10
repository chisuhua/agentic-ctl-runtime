// tests/test_httplib_version.cpp
// 文件头注释
// 功能描述：vendored cpp-httplib 版本守卫 — 确保升级到覆盖 client 侧
//          security advisories 的版本 (v0.54.1), 防止回退到带 CVE 的旧版
//          (CVE-2026-33745 + GHSA-39q5 + GHSA-h6wq + GHSA-c3h8, 详见
//          docs/audits/2026-09-10-adr-0087-sprint-24-httplib-status.md §3.5)
// 设计依据：openspec/changes/upgrade-httplib-0541/{proposal,design}.md
// 作者：AgenticDSL Sprint 25 (Oracle 评审 ses_f760c60d5ffe9hmgREbWz0hA8u)
// 最后修改日期：2026-09-10

#include "catch_amalgamated.hpp"

#include <httplib.h>
#include <string>
#include <utility>
#include <vector>

TEST_CASE("vendored cpp-httplib SHALL be v0.54.1 (security advisories covered)",
          "[httplib][security][version]") {
  // 4 个 client 侧 advisories 覆盖要求:
  // - CVE-2026-33745 (High 7.4)        → fixed in v0.39.0
  // - GHSA-39q5-hh6x-jpxx (High)       → fixed in v0.5x 系列 (恶意 Content-Length client 崩溃)
  // - GHSA-h6wq-j5mv-f3q8 (Moderate)   → fixed in v0.5x 系列 (负 chunk-size DoS, LLM streaming 直接暴露)
  // - GHSA-c3h8-fqq4-xm4g (High cond.)  → fixed in v0.5x 系列 (proxy redirect TLS bypass)
  // v0.54.1 (latest stable 2026-08-30) 一次性覆盖 4/4
  const std::string version = CPPHTTPLIB_VERSION;
  REQUIRE(version == "0.54.1");
}

TEST_CASE("httplib Headers SHALL be iterable-constructible (cloud_adapter L250 + http_adapter L172)",
          "[httplib][security][headers]") {
  // v0.52.0 将 Headers 改为 insertion-ordered multimap,
  // value_type 从 pair<const string, Mapped> → pair<string, Mapped>.
  // 回归 cloud_adapter.cpp:250 与 http_adapter.cpp:172 的迭代器构造必须仍可用
  // (D3 design 兼容策略验证).
  std::vector<std::pair<std::string, std::string>> vec;
  vec.emplace_back("Content-Type", "application/json");
  vec.emplace_back("Authorization", "Bearer test-key");
  httplib::Headers headers(vec.begin(), vec.end());
  REQUIRE(headers.size() == 2);
  REQUIRE(headers.count("Content-Type") == 1);
  REQUIRE(headers.count("Authorization") == 1);
  REQUIRE(headers.find("Content-Type")->second == "application/json");
  REQUIRE(headers.find("Authorization")->second == "Bearer test-key");
}
