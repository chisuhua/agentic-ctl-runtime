// examples/pdk_chat_demo/tests/test_code_review_skill.cpp
// U2 (Sprint 25+ carry-over) — code-review-run.skill.md v0.3 C++ 集成验证
// 设计依据: openspec/changes/code-review-run-skill-integration/
// 标签: [code_review_skill][integration]
// 作者: AgenticDSL U2 implementation
// 最后修改日期: 2026-09-04

#include "catch_amalgamated.hpp"

#include <agenticdsl/skill/skill_interpreter.h>
#include <agenticdsl/contract/iinteraction_bus.h>
#include <agenticdsl/contract/itool_registry.h>
#include <agenticdsl/types/layered_context.h>
#include "core/types/tool_result.h"
#include "test_helpers/mock_bus.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

using namespace agenticdsl;

// Mock 工具注册表：记录每次 call_tool 调用（spec.md:9, 21 可观察断言来源）
class MockToolRegistry : public IToolRegistry {
public:
    std::vector<std::pair<std::string, nlohmann::json>> calls;

    nlohmann::json call_tool(
        const std::string& name,
        const std::unordered_map<std::string, std::string>& args) override
    {
        nlohmann::json jargs;
        for (const auto& [k, v] : args) {
            jargs[k] = v;
        }
        calls.emplace_back(name, jargs);
        nlohmann::json reply;
        reply["result"] = "ok";
        reply["name"] = name;
        if (name == "fs.read") {
            reply["content"] = "mock file content for code review";
        } else if (name == "code_review/run") {
            reply["level"] = "thorough";
            reply["findings"] = nlohmann::json::array({
                {{"line", 1}, {"severity", "info"}, {"message", "mock review finding"}}
            });
        }
        return reply;
    }

    bool has_tool(const std::string&) const override { return true; }
    std::vector<std::string> list_tools() const override { return {"fs.read", "code_review/run"}; }
    void register_tool_function(std::string, ToolMetadata, ToolFunc) override {}
    void register_llm_tool(std::string, std::unique_ptr<ILLMTool>, const LLMParams&) override {}
    bool is_llm_tool(const std::string&) const override { return false; }
    const LLMParams& get_llm_params(const std::string&) const override {
        static LLMParams default_params;
        return default_params;
    }
    nlohmann::json call_llm_tool(const std::string&, const std::string&, const LLMParams&) override {
        return {{"content", "mock llm response"}};
    }
    void set_cost_callback(CostCallback) override {}
};

// 写临时文件并返回路径（用于 fixture skill 与缺失文件场景）
static std::string write_temp_file(const std::string& suffix, const std::string& content) {
    char path[] = "/tmp/u2_test_XXXXXX";
    if (mkstemp(path) < 0) return "";
    // mkstemp 返回的 path 已包含 \0，需要用 suffix 重新拼接后缀以便扩展名识别
    std::string final_path = std::string(path) + suffix;
    if (rename(path, final_path.c_str()) != 0) {
        // 若 rename 失败则使用原路径
        final_path = path;
    }
    std::ofstream f(final_path);
    if (!f.is_open()) return "";
    f << content;
    f.close();
    return final_path;
}

static void cleanup_file(const std::string& path) {
    if (!path.empty()) remove(path.c_str());
}

#ifdef __linux__

// 类别 A: 成功路径 — v0.3 skill 加载后调用 fs.read + code_review/run，
// output 应等于 code_review/run 的 mock 返回值，且 mock.calls 恰好两次
TEST_CASE("U2: code-review-run skill v0.3 success path", "[code_review_skill][integration]") {
    MockToolRegistry tools;
    test::MockBus bus;
    SkillInterpreter interpreter(tools, bus, nullptr, nullptr);

    std::string skill = write_temp_file(
        ".skill.md",
        "---\n"
        "name: code-review-run\n"
        "version: 0.3\n"
        "description: U2 integration test\n"
        "---\n"
        "call_tool(\"fs.read\", {\"path\": \"examples/pdk_chat_demo/main.cpp\"})\n"
        "call_tool(\"code_review/run\", {\"level\": \"thorough\"})\n"
        "return code_review_run\n");
    REQUIRE(!skill.empty());

    auto result = interpreter.run(skill, default_skill_capability());

    CHECK(result.success);
    CHECK(result.child_exit_status == 0);
    // mock registry 恰好记录两次调用
    REQUIRE(tools.calls.size() == 2);
    CHECK(tools.calls[0].first == "fs.read");
    CHECK(tools.calls[1].first == "code_review/run");
    // output == code_review/run mock 返回值（spec.md:9）
    CHECK(result.output.contains("findings"));

    cleanup_file(skill);
}

// 类别 B: skill 文件缺失 — run() 应返回 InvalidArg 且 stderr 非空
TEST_CASE("U2: skill file missing returns InvalidArg", "[code_review_skill][integration]") {
    MockToolRegistry tools;
    test::MockBus bus;
    SkillInterpreter interpreter(tools, bus, nullptr, nullptr);

    auto result = interpreter.run(
        "/tmp/this_path_does_not_exist_xyzzy.skill.md",
        default_skill_capability());

    CHECK_FALSE(result.success);
    CHECK(result.error_code == ErrorCode::InvalidArg);
    CHECK_FALSE(result.stderr_content.empty());
    CHECK(tools.calls.empty());

    // skill 不应保留
    struct stat st;
    CHECK(stat("/tmp/this_path_does_not_exist_xyzzy.skill.md", &st) != 0);
}

// 类别 C: 未授权工具拒绝（顶层 call_tool） — 父进程拒绝非致命，
// success==true, stderr 含诊断, mock.calls 无未授权工具
TEST_CASE("U2: unauthorized tool rejected non-fatally (top-level call_tool)", "[code_review_skill][integration]") {
    MockToolRegistry tools;
    test::MockBus bus;
    SkillInterpreter interpreter(tools, bus, nullptr, nullptr);

    // 临时 fixture：调用 shell/exec（不在 default_skill_capability 白名单）
    std::string skill = write_temp_file(
        ".skill.md",
        "---\n"
        "name: cap-test\n"
        "version: 0.1\n"
        "description: U2 capability rejection test\n"
        "---\n"
        "call_tool(\"shell/exec\", {\"cmd\": \"ls\"})\n"
        "return {}\n");
    REQUIRE(!skill.empty());

    auto result = interpreter.run(skill, default_skill_capability());

    // 非致命拒绝：success 仍 true（子进程继续执行到 return {}）
    CHECK(result.success);
    // stderr 应含拒绝诊断（IPC pipe_err → stderr_content）
    CHECK(result.stderr_content.find("shell/exec") != std::string::npos);
    // mock registry 不应有 shell/exec 调用记录（父进程在调用工具前拒绝）
    for (const auto& c : tools.calls) {
        CHECK(c.first != "shell/exec");
    }

    cleanup_file(skill);
}

#else  // !__linux__

// 类别 D: 非 Linux 平台降级 — run() 应返回 UnsupportedPlatform
TEST_CASE("U2: non-Linux platform returns UnsupportedPlatform", "[code_review_skill][integration]") {
    MockToolRegistry tools;
    test::MockBus bus;
    SkillInterpreter interpreter(tools, bus, nullptr, nullptr);

    std::string skill = write_temp_file(
        ".skill.md",
        "---\n"
        "name: noop\n"
        "version: 0.1\n"
        "description: non-Linux fallback\n"
        "---\n"
        "call_tool(\"fs.read\", {\"path\": \"x\"})\n"
        "return fs_read\n");
    REQUIRE(!skill.empty());

    auto result = interpreter.run(skill, default_skill_capability());

    CHECK_FALSE(result.success);
    CHECK(result.error_code == ErrorCode::UnsupportedPlatform);
    CHECK(tools.calls.empty());

    cleanup_file(skill);
}

#endif  // __linux__