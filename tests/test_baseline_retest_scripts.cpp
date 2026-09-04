// tests/test_baseline_retest_scripts.cpp
// Phase 7a baseline retest scripts Catch2 tests (per OpenSpec phase-7a-baseline-retest-v2)
// Decision 4: Catch2 + system()/popen spawn Python scripts (无 pytest 历史, 与既有 test_basic.cpp 一致)
// 5 cases per spec: PASS / Conditional / FAIL(parse<85) / FAIL(L1<70) / FAIL(L2<50)

#include "catch_amalgamated.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// Run shell command and capture stdout
std::string run_cmd(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    REQUIRE(pipe != nullptr);
    std::ostringstream out;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
        out << buf;
    }
    int rc = pclose(pipe);
    (void)rc;
    return out.str();
}

// Generate minimal baseline.json for a given scenario
void write_baseline(const std::string& path, double parse, double l1,
                    double l2, double l3, bool mock_mode = true,
                    const std::string& models_partial = "") {
    std::ofstream f(path);
    f << "{\n";
    f << "  \"baseline_id\": \"test-scenario\",\n";
    f << "  \"golden_tasks\": 54,\n";
    f << "  \"mock_mode\": " << (mock_mode ? "true" : "false") << ",\n";
    if (!models_partial.empty()) {
        f << "  \"models_partial\": [\"" << models_partial << "\"],\n";
    }
    f << "  \"llms\": {\n";
    f << "    \"model-a\": {\"parse_valid\": " << parse
      << ", \"task_success\": {\"L1\": " << l1
      << ", \"L2\": " << l2 << ", \"L3\": " << l3 << "}},\n";
    f << "    \"model-b\": {\"parse_valid\": " << parse
      << ", \"task_success\": {\"L1\": " << l1
      << ", \"L2\": " << l2 << ", \"L3\": " << l3 << "}}\n";
    f << "  },\n";
    f << "  \"generated_at\": \"2026-09-04T00:00:00Z\",\n";
    f << "  \"summary\": {\n";
    f << "    \"samples\": 100,\n";
    f << "    \"avg_parse_valid\": " << parse << ",\n";
    f << "    \"avg_task_success\": {\"L1\": " << l1
      << ", \"L2\": " << l2 << ", \"L3\": " << l3 << "}\n";
    f << "  }\n";
    f << "}\n";
}

}  // namespace

// Case A: PASS (95% parse + 80% L1/L2/L3, mock_mode=false — mock 强制 Conditional 故须 real 语义)
TEST_CASE("evidence-gate-v1.sh PASS verdict (95/80/80/80)", "[baseline][retest][case-a]") {
    write_baseline("/tmp/test_pass.json", 0.95, 0.80, 0.80, 0.80, false);
    std::string out = run_cmd("bash scripts/evidence-gate-v1.sh /tmp/test_pass.json 2>/dev/null");
    REQUIRE(out.find("\"verdict\": \"PASS\"") != std::string::npos);
}

// Case G: mock_mode=true 强制 Conditional (per spec "Mock mode" scenario + F1 修复)
TEST_CASE("evidence-gate-v1.sh mock_mode forces Conditional (F1 fix)", "[baseline][retest][case-g]") {
    write_baseline("/tmp/test_mock.json", 0.95, 0.80, 0.80, 0.80, true);
    std::string out = run_cmd("bash scripts/evidence-gate-v1.sh /tmp/test_mock.json 2>/dev/null");
    REQUIRE(out.find("\"verdict\": \"Conditional\"") != std::string::npos);
    REQUIRE(out.find("mock_baseline") != std::string::npos);
}

// Case B: Conditional (88% parse + 75% L1)
TEST_CASE("evidence-gate-v1.sh Conditional verdict (88/75)", "[baseline][retest][case-b]") {
    write_baseline("/tmp/test_cond.json", 0.88, 0.75, 0.50, 0.30);
    std::string out = run_cmd("bash scripts/evidence-gate-v1.sh /tmp/test_cond.json 2>/dev/null");
    REQUIRE(out.find("\"verdict\": \"Conditional\"") != std::string::npos);
}

// Case C: FAIL parse_valid < 85% (80% parse + 80% L1)
TEST_CASE("evidence-gate-v1.sh FAIL parse<85", "[baseline][retest][case-c]") {
    write_baseline("/tmp/test_fail_parse.json", 0.80, 0.80, 0.80, 0.80);
    std::string out = run_cmd("bash scripts/evidence-gate-v1.sh /tmp/test_fail_parse.json 2>/dev/null");
    REQUIRE(out.find("\"verdict\": \"FAIL\"") != std::string::npos);
}

// Case D: FAIL L1 < 70% (95% parse + 60% L1)
TEST_CASE("evidence-gate-v1.sh FAIL L1<70", "[baseline][retest][case-d]") {
    write_baseline("/tmp/test_fail_l1.json", 0.95, 0.60, 0.80, 0.80);
    std::string out = run_cmd("bash scripts/evidence-gate-v1.sh /tmp/test_fail_l1.json 2>/dev/null");
    REQUIRE(out.find("\"verdict\": \"FAIL\"") != std::string::npos);
}

// Case E: FAIL L2 < 50% with parse ≥90 (per Oracle B2 判定表空洞修复)
TEST_CASE("evidence-gate-v1.sh FAIL L2<50 with parse≥90", "[baseline][retest][case-e]") {
    write_baseline("/tmp/test_fail_l2.json", 0.92, 0.80, 0.40, 0.35);
    std::string out = run_cmd("bash scripts/evidence-gate-v1.sh /tmp/test_fail_l2.json 2>/dev/null");
    REQUIRE(out.find("\"verdict\": \"FAIL\"") != std::string::npos);
}

// Case F: --write-markdown output (Decision 5, real 语义 mock_mode=false 因 F1 mock 强制 Conditional)
TEST_CASE("evidence-gate-v1.sh --write-markdown writes file", "[baseline][retest][case-f]") {
    write_baseline("/tmp/test_wm.json", 0.95, 0.80, 0.80, 0.80, false);
    std::string out = run_cmd(
        "bash scripts/evidence-gate-v1.sh /tmp/test_wm.json --write-markdown /tmp/test_wm.md 2>/dev/null");
    REQUIRE(out.find("\"verdict\": \"PASS\"") != std::string::npos);
    // verify markdown file exists and contains "Verdict: PASS"
    std::ifstream md("/tmp/test_wm.md");
    REQUIRE(md.good());
    std::stringstream ss; ss << md.rdbuf();
    // markdown 含粗体 "**Verdict**: PASS"; control-plane-eval.py 正则允许 `\*?\*?`
    std::string content = ss.str();
    // 提取 verdict (去除 markdown 粗体标记)
    size_t vpos = content.find("Verdict");
    REQUIRE(vpos != std::string::npos);
    size_t colon = content.find(':', vpos);
    REQUIRE(colon != std::string::npos);
    REQUIRE(content.substr(colon + 1).find("PASS") != std::string::npos);
}
