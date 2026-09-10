// examples/pdk_chat_demo/tests/test_helpers/real_llm_env.h
// chat-real-llm-coverage Phase C.1: 真实 LLM env 行为变更 helper
//
// API 抽离:
//   - require_real_llm_env() — 直接调 Catch2 FAIL (无 try/catch 多余样板)
//   - real_llm_config()       — 从 env 构造 LLMConfig (deepseek 优先)
//   - real_llm_provider()     — factory.create() wrapper
//
// 行为变更 (env var 真值表):
//   HYDRAFORGE_SKIP_REAL_LLM=1     → 静默 return (opt-in skip)
//   else + key set (DEEPSEEK/MINIMAX) → run
//   else + no key                   → FAIL (硬失败)
//
// 设计依据 (Oracle 审查):
//   - 路径: examples/pdk_chat_demo/tests/test_helpers/ (与消费方同 dir)
//   - API: 直接 FAIL (无不存在类型 Catch2_failure)
//   - ILLMProvider: common/llm/llm_types.h (非 include/agenticdsl/llm/...)
//   - Header-only inline (与 tests/test_helpers/http_mock_server.h 一致)
//
// 用法 (per test):
//   #include "test_helpers/real_llm_env.h"
//   TEST_CASE("...real LLM test...", "[realllm]") {
//     pdk_chat_demo::testing::require_real_llm_env();
//     // ... test body (API key guaranteed non-empty below) ...
//   }

#pragma once

#include <cstdlib>
#include <memory>
#include <string>

#include <catch_amalgamated.hpp>

#include "common/llm/llm_types.h"           // ILLMProvider (src/common/llm/llm_types.h)
#include "common/llm/llm_provider_factory.h" // LLMProviderFactory
#include "common/llm/llm_config.h"           // LLMConfig

namespace pdk_chat_demo::testing {

// ===== env var 真值表 =====
//
// 单一职责 — 直接调 Catch2 FAIL:
//   - skip flag set      → 静默 return
//   - key set (deepseek) → return, key is DEEPSEEK_API_KEY
//   - key set (minimax)  → return, key is MINIMAX_API_KEY
//   - no key + no skip   → FAIL("real LLM env required: ...")
//
// 调用方不需要 try/catch (Catch2 FAIL 内部抛异常, Catch2 标记当前 TEST_CASE 为 FAIL).
inline void require_real_llm_env() {
  // 1. opt-in skip
  if (const char* skip = std::getenv("HYDRAFORGE_SKIP_REAL_LLM");
      skip && std::string(skip) == "1") {
    return;  // 静默 skip — 测试应自行 short-circuit
  }
  // 2. DEEPSEEK_API_KEY set → ok
  if (const char* ds = std::getenv("DEEPSEEK_API_KEY");
      ds && ds[0] != '\0') {
    return;
  }
  // 3. MINIMAX_API_KEY set → ok
  if (const char* mm = std::getenv("MINIMAX_API_KEY");
      mm && mm[0] != '\0') {
    return;
  }
  // 4. 无 key + 无 skip → 硬失败
  FAIL("real LLM env required: set DEEPSEEK_API_KEY or MINIMAX_API_KEY, "
       "or set HYDRAFORGE_SKIP_REAL_LLM=1 to opt-in skip");
}

// Helper struct — 暴露从 env 解析后的 provider config
struct RealLLMConfig {
  std::string provider;       // "deepseek" | "minimax"
  std::string model;          // e.g. "deepseek-v4-flash"
  std::string api_url;        // provider endpoint
  std::string api_endpoint;   // path
  std::string api_key;        // 取自 env (helper 内部绝不 log 此字段)
  std::string api_key_env;    // "DEEPSEEK_API_KEY" | "MINIMAX_API_KEY"
  std::string env_used;       // 同 api_key_env (冗余, 便于日志)
};

// 从 env 构造配置 (deepseek 优先, fallback minimax)
// 假定 require_real_llm_env() 已调用 (key 非空已保证)
inline RealLLMConfig real_llm_config() {
  RealLLMConfig cfg;
  if (const char* ds = std::getenv("DEEPSEEK_API_KEY");
      ds && ds[0] != '\0') {
    cfg.provider = "deepseek";
    // ⚠️ NOT redundant: 使用 "deepseek-chat" 别名（非 "deepseek-v4-flash"）
    // 原因：DeepSeek API 行为变更（2026-09 实证）—— model 名 "deepseek-v4-flash"
    // 触发 reasoning mode（response.reasoning_content 耗光所有 token,
    // response.content 永远 ""），"deepseek-chat" 映射到同一 v4-flash 引擎
    // 但不触发 reasoning（response.content 正常返回）。
    // 双 helper 同步（per AGENTS.md §治理层 模式 #5）。
    cfg.model = "deepseek-chat";
    cfg.api_url = "https://api.deepseek.com";
    cfg.api_endpoint = "/chat/completions";
    cfg.api_key = ds;
    cfg.api_key_env = "DEEPSEEK_API_KEY";
  } else if (const char* mm = std::getenv("MINIMAX_API_KEY");
             mm && mm[0] != '\0') {
    cfg.provider = "minimax";
    cfg.model = "minimax-text-01";
    cfg.api_url = "https://api.minimax.chat";  // placeholder URL, 当前未使用
    cfg.api_endpoint = "/v1/text/chatcompletion_v2";
    cfg.api_key = mm;
    cfg.api_key_env = "MINIMAX_API_KEY";
  }
  cfg.env_used = cfg.api_key_env;
  return cfg;
}

// 构造 LLMProvider 实例 (返回 unique_ptr, 调用方持有)
inline std::unique_ptr<agenticdsl::ILLMProvider> real_llm_provider() {
  auto cfg = real_llm_config();
  agenticdsl::LLMConfig llm_cfg;
  llm_cfg.provider = cfg.provider;
  llm_cfg.model = cfg.model;
  llm_cfg.api_url = cfg.api_url;
  llm_cfg.api_endpoint = cfg.api_endpoint;
  llm_cfg.api_key = cfg.api_key;
  llm_cfg.api_key_env = cfg.api_key_env;

  agenticdsl::LLMProviderFactory factory;
  return factory.create(llm_cfg);
}

}  // namespace pdk_chat_demo::testing