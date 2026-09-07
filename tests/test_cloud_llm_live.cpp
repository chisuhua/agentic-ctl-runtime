// tests/test_cloud_llm_live.cpp
/**
 * @file test_cloud_llm_live.cpp
 * @brief CloudLLMAdapter Live 测试（需要真实 API key，默认禁用）
 * @date 2026-06-07
 *
 * 默认不参与 ctest，所有测试用例由 AGENTICDSL_ENABLE_LIVE_TESTS 宏保护。
 * 启用方法：
 *   cmake -DAGENTICDSL_ENABLE_LIVE_TESTS=ON ..
 *   export DEEPSEEK_API_KEY=sk-...
 *   make test_cloud_llm_live && ctest -L live
 *
 * 安全约束：
 * - 严禁提交真实 API key
 * - 真实请求会被发送至 DeepSeek (OpenAI 兼容协议)，消耗 token 与额度
 * - 仅在开发与 CI 验证场景启用
 */

// 即使宏未定义，文件也可编译（Catch2 主程序仍由 catch_amalgamated.cpp 提供）
#include "catch_amalgamated.hpp"

#include "common/llm/cloud_adapter.h"
#include "common/llm/llm_config.h"

#include <cstdlib>
#include <string>

using namespace agenticdsl;

namespace {

// 辅助：从环境变量读取 API key（仅在 live 模式下使用）
std::string get_api_key_from_env(const char* env_name) {
  if (const char* val = std::getenv(env_name)) {
    return std::string(val);
  }
  return std::string{};
}

} // namespace

#ifdef AGENTICDSL_ENABLE_LIVE_TESTS

// ================================
// DeepSeek Live Tests
// ================================
//
// DeepSeek API 兼容 OpenAI Chat Completions 协议。
// CloudLLMAdapter 必须显式设置 api_url = "https://api.deepseek.com" 和
// api_endpoint = "/chat/completions" — LLMConfig 默认值是 OpenAI 的 URL 和
// "/v1/chat/completions"，直接组合会导致 /v1/v1/chat/completions 404。
//
// 同时 GenerationRequest 必须显式设置 req.params.model，否则
// build_request_body 会使用 LLMConfig::model 的默认值 "gpt-4o-mini"，
// DeepSeek 端会拒绝该模型。
//
// 模型选择：使用 DeepSeek v4 系列模型名（用户实际部署的端点支持）。
// 如官方 deepseek-chat 也可，但当前 API 端点接受 v4 系列。

TEST_CASE("CloudLLMAdapter DeepSeek live - sync generate", "[cloud_llm][live]") {
  std::string api_key = get_api_key_from_env("DEEPSEEK_API_KEY");
  if (api_key.empty()) {
    SKIP("DEEPSEEK_API_KEY not set, skipping live test");
  }

  LLMConfig cfg;
  cfg.provider     = "deepseek";
  cfg.api_key      = api_key;
  cfg.model        = "deepseek-v4-flash";
  cfg.api_url      = "https://api.deepseek.com";
  cfg.api_endpoint = "/chat/completions";
  cfg.max_tokens   = 256;          // deepseek-v4-flash 是 reasoning model,需要 >128 token 给推理 + 输出
  cfg.timeout_seconds = 30;

  CloudLLMAdapter adapter(cfg);
  REQUIRE(adapter.is_available());

  GenerationRequest req;
  req.prompt         = "Reply with the single word 'pong' and nothing else.";
  req.params.model   = "deepseek-v4-flash";  // 必须显式设置,否则会用 LLMConfig 默认 "gpt-4o-mini"
  req.params.max_tokens = 256;

  auto result = adapter.generate(req, {});
  if (!result.has_value()) {
    // 网络错误 / 限流等：记录但不强制失败
    WARN("Live generate failed: " << result.error().message);
    return;
  }
  CHECK_FALSE(result.value().text.empty());
  CHECK(result.value().completion_tokens > 0);
}

TEST_CASE("CloudLLMAdapter DeepSeek live - stream generate", "[cloud_llm][live][stream]") {
  std::string api_key = get_api_key_from_env("DEEPSEEK_API_KEY");
  if (api_key.empty()) {
    SKIP("DEEPSEEK_API_KEY not set, skipping live test");
  }

  LLMConfig cfg;
  cfg.provider     = "deepseek";
  cfg.api_key      = api_key;
  cfg.model        = "deepseek-v4-flash";
  cfg.api_url      = "https://api.deepseek.com";
  cfg.api_endpoint = "/chat/completions";
  cfg.max_tokens   = 256;          // reasoning model 需要充足 token 给 reasoning_content + 实际输出
  cfg.timeout_seconds = 30;

  CloudLLMAdapter adapter(cfg);
  GenerationRequest req;
  req.prompt         = "Count from 1 to 5.";
  req.params.model   = "deepseek-v4-flash";
  req.params.max_tokens = 256;

  auto stream = adapter.generate_stream(req, {});
  REQUIRE(stream != nullptr);

  std::string accumulated;
  int token_count = 0;
  while (auto token = stream->next({})) {
    accumulated += *token;
    token_count++;
    if (token_count > 100)
      break; // 安全：避免无限循环
  }

  CHECK_FALSE(accumulated.empty());
}

TEST_CASE("CloudLLMAdapter DeepSeek 401 maps to AuthenticationError", "[cloud_llm][live][error]") {
  LLMConfig cfg;
  cfg.provider     = "deepseek";
  cfg.api_key      = "sk-invalid-key-for-testing-401-response";
  cfg.model        = "deepseek-v4-flash";
  cfg.api_url      = "https://api.deepseek.com";
  cfg.api_endpoint = "/chat/completions";
  cfg.max_tokens   = 8;
  cfg.timeout_seconds = 10;

  CloudLLMAdapter adapter(cfg);
  GenerationRequest req;
  req.prompt       = "hi";
  req.params.model = "deepseek-v4-flash";

  auto result = adapter.generate(req, {});
  if (result.has_value()) {
    WARN("Expected 401 but got success - test may be running in mock mode");
    return;
  }
  // 可能是 AuthenticationError 或 NetworkError（取决于网络环境）
  CHECK((result.error().code == LLMError::Code::AuthenticationError ||
         result.error().code == LLMError::Code::NetworkError));
}

#else // AGENTICDSL_ENABLE_LIVE_TESTS 未定义

// 默认构建时，至少提供一个 no-op 测试，确保文件被识别为有效 Catch2 测试源
TEST_CASE("CloudLLMAdapter live tests disabled", "[cloud_llm][live][disabled]") {
  SUCCEED("Live tests disabled. Set AGENTICDSL_ENABLE_LIVE_TESTS=ON to enable.");
}

#endif // AGENTICDSL_ENABLE_LIVE_TESTS
