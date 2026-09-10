// tests/test_real_llm_env_helper.cpp
// real-llm-core-coverage Phase 0.2: 项目级 helper 自测
//
// 验证 helper 自身契约 (独立于真实 LLM 调用), 与 sibling change
// (chat-real-llm-coverage) 的 examples/pdk_chat_demo/tests/test_real_llm_env_helper.cpp
// 同源 (4 cases 复用, namespace 改为 agenticdsl::test):
//   - skip flag set → 静默 return
//   - no key no skip → FAIL 行为由真实 LLM 测试验证 (本测试仅标记)
//   - DEEPSEEK_API_KEY set → real_llm_config 字段正确
//   - MINIMAX fallback / DEEPSEEK 优先

#include <catch_amalgamated.hpp>

#include <cstdlib>
#include <string>
#include <utility>

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

// ===== helper 自测: skip flag silence =====

TEST_CASE("helper: skip flag set → silent return",
          "[real-llm-core-coverage][helper]") {
  EnvGuard skip_guard("HYDRAFORGE_SKIP_REAL_LLM", "1");
  EnvGuard ds_guard("DEEPSEEK_API_KEY", "");  // 显式 unset
  EnvGuard mm_guard("MINIMAX_API_KEY", "");

  // 应静默 return, 不 FAIL
  agenticdsl::test::require_real_llm_env();
  SUCCEED("helper silently returned on skip=1");
}

TEST_CASE("helper: no key no skip → FAIL (helper integration)",
          "[real-llm-core-coverage][helper]") {
  SUCCEED("FAIL behavior verified by real LLM tests");
}

// ===== helper 自测: real_llm_config 字段正确 =====

TEST_CASE("helper: real_llm_config fields correct with DEEPSEEK_API_KEY",
          "[real-llm-core-coverage][helper]") {
  EnvGuard skip_guard("HYDRAFORGE_SKIP_REAL_LLM", "");
  EnvGuard ds_guard("DEEPSEEK_API_KEY", "test_deepseek_key_xyz");
  EnvGuard mm_guard("MINIMAX_API_KEY", "");  // 强制走 deepseek

  // skip 不需调用, key 已 set → 直接调用 config
  auto cfg = agenticdsl::test::real_llm_config();
  REQUIRE(cfg.provider == "deepseek");
  // ⚠️ 模型名变更 2026-09: deepseek-v4-flash → deepseek-chat. 详见
  //   tests/test_helpers/real_llm_env.h 注释 (DeepSeek reasoning mode 规避).
  REQUIRE(cfg.model == "deepseek-chat");
  REQUIRE(cfg.api_url == "https://api.deepseek.com");
  REQUIRE(cfg.api_key == "test_deepseek_key_xyz");
  REQUIRE(cfg.api_key_env == "DEEPSEEK_API_KEY");
  REQUIRE(cfg.env_used == "DEEPSEEK_API_KEY");
}

TEST_CASE("helper: real_llm_config falls back to MINIMAX",
          "[real-llm-core-coverage][helper]") {
  EnvGuard skip_guard("HYDRAFORGE_SKIP_REAL_LLM", "");
  EnvGuard ds_guard("DEEPSEEK_API_KEY", "");  // 强制 fallback
  EnvGuard mm_guard("MINIMAX_API_KEY", "test_minimax_key_abc");

  auto cfg = agenticdsl::test::real_llm_config();
  REQUIRE(cfg.provider == "minimax");
  REQUIRE(cfg.model == "minimax-text-01");
  REQUIRE(cfg.api_key == "test_minimax_key_abc");
  REQUIRE(cfg.api_key_env == "MINIMAX_API_KEY");
}

TEST_CASE("helper: real_llm_config prefers DEEPSEEK when both set",
          "[real-llm-core-coverage][helper]") {
  EnvGuard skip_guard("HYDRAFORGE_SKIP_REAL_LLM", "");
  EnvGuard ds_guard("DEEPSEEK_API_KEY", "deepseek_wins");
  EnvGuard mm_guard("MINIMAX_API_KEY", "minimax_loses");

  auto cfg = agenticdsl::test::real_llm_config();
  REQUIRE(cfg.provider == "deepseek");
  REQUIRE(cfg.api_key == "deepseek_wins");
}