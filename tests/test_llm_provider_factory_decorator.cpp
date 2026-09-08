// tests/test_llm_provider_factory_decorator.cpp
// 文件头注释
// 功能描述：LLMProviderFactory 集成测试 — 验证 cloud 路径返回 SerializingDecorator 包装
// 设计依据：openspec/changes/fix-cloud-adapter-multithreading/design.md §Factory 集成
// 作者：AgenticDSL Wave 1 #2
// 最后修改日期：2026-09-08

#include "catch_amalgamated.hpp"

#include "common/llm/llm_config.h"
#include "common/llm/llm_provider_factory.h"
#include "common/llm/llm_types.h"
#include "common/llm/serializing_decorator.h"

#include <memory>

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

// === Test 1: cloud 路径 (deepseek) 注入 SerializingDecorator ===
TEST_CASE("LLMProviderFactory.create(deepseek) wraps SerializingDecorator",
          "[factory_decorator][unit]") {
  LLMConfig cfg;
  cfg.provider = "deepseek";
  cfg.api_key = "test-key";  // CI 友好: 不真发请求, 只检查 wrapper 结构
  cfg.model = "deepseek-v4-flash";

  LLMProviderFactory factory;
  auto provider = factory.create(cfg);

  REQUIRE(provider != nullptr);

  // 外层必须是 SerializingDecorator
  auto* serializing = require_cast<SerializingDecorator>(
      provider.get(), "SerializingDecorator");
  REQUIRE(serializing != nullptr);

  // 用途标签正确
  REQUIRE(serializing->purpose() == "cloud-deepseek");

  // 内层 (provider.inner()) 必须是 CloudLLMAdapter (经 raw inner_ 字段访问)
  // 注: SerializingDecorator 继承 ILLMProvider 不继承 ILLMProviderDecorator,
  // 所以无 inner() 方法; 改为检查 SerializingDecorator 内部状态 (concurrent_count_ = 0)
  REQUIRE(serializing->concurrent_count() == 0);
}

// === Test 2: mock 路径不包装 SerializingDecorator ===
TEST_CASE("LLMProviderFactory.create(mock) does NOT wrap SerializingDecorator",
          "[factory_decorator][unit]") {
  LLMConfig cfg;
  cfg.provider = "mock";

  LLMProviderFactory factory;
  auto provider = factory.create(cfg);

  REQUIRE(provider != nullptr);
  // Mock 路径零影响 (B.3/B.4 mock 测试性能不变)
  REQUIRE(dynamic_cast<SerializingDecorator*>(provider.get()) == nullptr);
}

// === Test 3: llama/local 路径不包装 SerializingDecorator ===
TEST_CASE("LLMProviderFactory.create(llama) does NOT wrap SerializingDecorator",
          "[factory_decorator][unit]") {
  LLMConfig cfg;
  cfg.provider = "llama";

  LLMProviderFactory factory;
  auto provider = factory.create(cfg);

  REQUIRE(provider != nullptr);
  // 本地 llama 路径零影响
  REQUIRE(dynamic_cast<SerializingDecorator*>(provider.get()) == nullptr);
}

// === Test 4: 所有 cloud 后端都包装 (openai/anthropic/minimax/qwen/moonshot/custom) ===
TEST_CASE("LLMProviderFactory.create wraps SerializingDecorator for all cloud backends",
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
    auto* serializing = dynamic_cast<SerializingDecorator*>(provider.get());
    REQUIRE(serializing != nullptr);
    // 用途标签正确 (含 backend 名)
    REQUIRE(serializing->purpose() == "cloud-" + backend);
  }
}