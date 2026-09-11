// tests/test_llm_provider_factory_decorator.cpp
// 文件头注释
// 功能描述：LLMProviderFactory 集成测试 — 验证 cloud 路径 OPT-IN SerializingDecorator
//          包装 (ADR-0087 Step 4 默认无包装, opts.serializer = true 显式启用)
// 设计依据：openspec/changes/adr-0087-root-cause-upgrade/design.md §Decision 3
//          (Wave 1 #2 原始 fix-cloud-adapter-multithreading 默认包装行为已反转)
// 作者：AgenticDSL Wave 1 #2 (Step 1-3 ship) + Sprint 27 Step 4 (this file)
// 最后修改日期：2026-09-11

#include "catch_amalgamated.hpp"

#include "common/llm/llm_config.h"
#include "common/llm/llm_provider_factory.h"
#include "common/llm/llm_types.h"
#include "common/llm/serializing_decorator.h"

#include <memory>
#include <string>
#include <vector>

using namespace agenticdsl;

namespace {

// helper: 动态转型校验, 失败时给出清晰诊断
template <typename T>
T* require_cast(ILLMProvider* p, const char* type_name) {
  T* casted = dynamic_cast<T*>(p);
  if (!casted) {
    FAIL("Expected " << type_name << " but got different type");
  }
  return casted;
}

}  // namespace

// === Test 1: cloud 路径 (deepseek) 默认路径 — ADR-0087 Step 4 默认无包装 ===
TEST_CASE("LLMProviderFactory.create(deepseek) does NOT wrap SerializingDecorator by default",
          "[factory_decorator][unit]") {
  LLMConfig cfg;
  cfg.provider = "deepseek";
  cfg.api_key = "test-key";
  cfg.model = "deepseek-v4-flash";

  LLMProviderFactory factory;
  auto provider = factory.create(cfg);

  REQUIRE(provider != nullptr);
  // ADR-0087 Step 4: root cause 升级后默认无 SerializingDecorator
  REQUIRE(dynamic_cast<SerializingDecorator*>(provider.get()) == nullptr);
}

TEST_CASE("LLMProviderFactory.create(mock) does NOT wrap SerializingDecorator",
          "[factory_decorator][unit]") {
  LLMConfig cfg;
  cfg.provider = "mock";

  LLMProviderFactory factory;
  auto provider = factory.create(cfg);

  REQUIRE(provider != nullptr);
  REQUIRE(dynamic_cast<SerializingDecorator*>(provider.get()) == nullptr);
}

TEST_CASE("LLMProviderFactory.create(llama) does NOT wrap SerializingDecorator",
          "[factory_decorator][unit]") {
  LLMConfig cfg;
  cfg.provider = "llama";

  LLMProviderFactory factory;
  auto provider = factory.create(cfg);

  REQUIRE(provider != nullptr);
  REQUIRE(dynamic_cast<SerializingDecorator*>(provider.get()) == nullptr);
}

// === Test 4: 所有 cloud 后端默认都无 SerializingDecorator (Step 4 反转 Wave 1 #2 行为) ===
TEST_CASE("LLMProviderFactory.create does NOT wrap SerializingDecorator for any cloud backend (default)",
          "[factory_decorator][unit]") {
  const std::vector<std::string> cloud_backends = {
      "openai", "anthropic", "deepseek", "minimax", "qwen", "moonshot", "custom"};
  LLMProviderFactory factory;

  for (const auto& backend : cloud_backends) {
    LLMConfig cfg;
    cfg.provider = backend;
    cfg.api_key = "test-key";

    auto provider = factory.create(cfg);
    REQUIRE(provider != nullptr);
    REQUIRE(dynamic_cast<SerializingDecorator*>(provider.get()) == nullptr);
  }
}

TEST_CASE("LLMProviderFactory.create wraps SerializingDecorator when opts.serializer = true",
          "[factory_decorator][unit]") {
  const std::vector<std::string> cloud_backends = {
      "openai", "anthropic", "deepseek", "minimax", "qwen", "moonshot", "custom"};
  LLMProviderFactory factory;
  LLMProviderFactory::CreateOptions opts;
  opts.serializer = true;

  for (const auto& backend : cloud_backends) {
    LLMConfig cfg;
    cfg.provider = backend;
    cfg.api_key = "test-key";

    auto provider = factory.create(cfg, opts);
    REQUIRE(provider != nullptr);
    auto* serializing = require_cast<SerializingDecorator>(
        provider.get(), "SerializingDecorator");
    REQUIRE(serializing != nullptr);
    REQUIRE(serializing->purpose() == "cloud-" + backend);
    REQUIRE(serializing->concurrent_count() == 0);
  }
}

TEST_CASE("LLMProviderFactory.create with opts.serializer = false explicit matches default",
          "[factory_decorator][unit]") {
  LLMProviderFactory factory;
  LLMProviderFactory::CreateOptions opts;
  opts.serializer = false;

  LLMConfig cfg;
  cfg.provider = "deepseek";
  cfg.api_key = "test-key";

  auto provider = factory.create(cfg, opts);
  REQUIRE(provider != nullptr);
  REQUIRE(dynamic_cast<SerializingDecorator*>(provider.get()) == nullptr);
}

TEST_CASE("LLMProviderFactory.create with opts.serializer = true does NOT wrap mock/llama",
          "[factory_decorator][unit]") {
  LLMProviderFactory factory;
  LLMProviderFactory::CreateOptions opts;
  opts.serializer = true;

  LLMConfig mock_cfg;
  mock_cfg.provider = "mock";
  auto mock_provider = factory.create(mock_cfg, opts);
  REQUIRE(mock_provider != nullptr);
  REQUIRE(dynamic_cast<SerializingDecorator*>(mock_provider.get()) == nullptr);

  LLMConfig llama_cfg;
  llama_cfg.provider = "llama";
  auto llama_provider = factory.create(llama_cfg, opts);
  REQUIRE(llama_provider != nullptr);
  REQUIRE(dynamic_cast<SerializingDecorator*>(llama_provider.get()) == nullptr);
}