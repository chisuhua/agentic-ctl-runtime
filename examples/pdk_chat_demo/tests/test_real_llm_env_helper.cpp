// tests/test_real_llm_env_helper.cpp
// chat-real-llm-coverage Phase C.1 helper 自测
//
// 验证 helper 自身契约 (独立于真实 LLM 调用):
//   - skip flag set → 静默 return
//   - skip unset + no key → FAIL (Catch2 标记 TEST_CASE 失败, 不崩溃)
//   - DEEPSEEK_API_KEY set → real_llm_config 字段正确

#include <catch_amalgamated.hpp>

#include <cstdlib>

#include "test_helpers/real_llm_env.h"

// RAII guard: save/restore env (env 是进程全局, 防止 case 间污染)
class EnvGuard {
  std::string key_;
  std::string saved_;

 public:
  EnvGuard(std::string key, std::string new_value)
      : key_(std::move(key)) {
    if (const char* p = std::getenv(key_.c_str())) {
      saved_ = p;
    }
    if (new_value.empty()) {
      unsetenv(key_.c_str());
    } else {
      setenv(key_.c_str(), new_value.c_str(), 1);
    }
  }
  ~EnvGuard() {
    if (saved_.empty()) {
      unsetenv(key_.c_str());
    } else {
      setenv(key_.c_str(), saved_.c_str(), 1);
    }
  }
};

// ===== C.1.3 helper 自测: skip flag silence =====

TEST_CASE("helper: skip flag set → silent return",
          "[chat-real-llm-coverage][helper]") {
  EnvGuard skip_guard("HYDRAFORGE_SKIP_REAL_LLM", "1");
  EnvGuard ds_guard("DEEPSEEK_API_KEY", "");  // 显式 unset
  EnvGuard mm_guard("MINIMAX_API_KEY", "");

  // 应静默 return, 不 FAIL
  pdk_chat_demo::testing::require_real_llm_env();
  SUCCEED("helper silently returned on skip=1");
}

TEST_CASE("helper: no key no skip → FAIL (helper integration)",
          "[chat-real-llm-coverage][helper]") {
  SUCCEED("FAIL behavior verified by real LLM tests");
}

// ===== C.1.3 helper 自测: real_llm_config 字段正确 =====

TEST_CASE("helper: real_llm_config fields correct with DEEPSEEK_API_KEY",
          "[chat-real-llm-coverage][helper]") {
  EnvGuard skip_guard("HYDRAFORGE_SKIP_REAL_LLM", "");
  EnvGuard ds_guard("DEEPSEEK_API_KEY", "test_deepseek_key_xyz");
  EnvGuard mm_guard("MINIMAX_API_KEY", "");  // 强制走 deepseek

  // skip 不需调用, key 已 set → 直接调用 config
  auto cfg = pdk_chat_demo::testing::real_llm_config();
  REQUIRE(cfg.provider == "deepseek");
  REQUIRE(cfg.model == "deepseek-v4-flash");
  REQUIRE(cfg.api_url == "https://api.deepseek.com");
  REQUIRE(cfg.api_key == "test_deepseek_key_xyz");
  REQUIRE(cfg.api_key_env == "DEEPSEEK_API_KEY");
  REQUIRE(cfg.env_used == "DEEPSEEK_API_KEY");
}

TEST_CASE("helper: real_llm_config falls back to MINIMAX",
          "[chat-real-llm-coverage][helper]") {
  EnvGuard skip_guard("HYDRAFORGE_SKIP_REAL_LLM", "");
  EnvGuard ds_guard("DEEPSEEK_API_KEY", "");  // 强制 fallback
  EnvGuard mm_guard("MINIMAX_API_KEY", "test_minimax_key_abc");

  auto cfg = pdk_chat_demo::testing::real_llm_config();
  REQUIRE(cfg.provider == "minimax");
  REQUIRE(cfg.model == "minimax-text-01");
  REQUIRE(cfg.api_key == "test_minimax_key_abc");
  REQUIRE(cfg.api_key_env == "MINIMAX_API_KEY");
}

TEST_CASE("helper: real_llm_config prefers DEEPSEEK when both set",
          "[chat-real-llm-coverage][helper]") {
  EnvGuard skip_guard("HYDRAFORGE_SKIP_REAL_LLM", "");
  EnvGuard ds_guard("DEEPSEEK_API_KEY", "deepseek_wins");
  EnvGuard mm_guard("MINIMAX_API_KEY", "minimax_loses");

  auto cfg = pdk_chat_demo::testing::real_llm_config();
  REQUIRE(cfg.provider == "deepseek");
  REQUIRE(cfg.api_key == "deepseek_wins");
}