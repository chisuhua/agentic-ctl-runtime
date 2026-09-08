#include "common/llm/llm_provider_factory.h"

#include <mutex>

#include "common/llm/cloud_adapter.h"        // CloudLLMAdapter (OpenAI 兼容协议)
#include "common/llm/llama_adapter_provider.h"  // LlamaAdapterProvider (本地 llama.cpp)
#include "common/llm/llm_config.h"          // LLMConfig
#include "common/llm/llm_types.h"            // ILLMProvider
#include "common/llm/mock_provider_factory.h"  // MockProviderFactory
#include "common/llm/serializing_decorator.h"  // SerializingDecorator (Wave 1 #2: 多线程 SIGSEGV 修复)

#include <stdexcept>

namespace agenticdsl {

// 内置 CloudProviderFactory (P1.T1: 避免 YAGNI 多文件)
class CloudProviderFactory : public IProviderFactory {
 public:
  std::unique_ptr<ILLMProvider> create(const LLMConfig& config) override {
    return std::make_unique<CloudLLMAdapter>(config);
  }
};

// 内置 LlamaProviderFactory (P1.T1: LLMConfig → LlamaAdapter::Config 字段映射)
class LlamaProviderFactory : public IProviderFactory {
 public:
  std::unique_ptr<ILLMProvider> create(const LLMConfig& config) override {
    // LlamaAdapter::Config 实际字段: api_url/api_endpoint/api_key/model/n_ctx/n_threads/temperature/n_predict
    // (无 model_path / min_p 字段)
    LlamaAdapter::Config llama_config;
    llama_config.api_url = config.api_url;
    llama_config.api_endpoint = config.api_endpoint;
    llama_config.api_key = config.resolve_api_key();  // 优先 env > file > 字段
    llama_config.model = config.model;
    llama_config.n_ctx = config.n_ctx;
    llama_config.n_threads = config.n_threads;
    llama_config.temperature = config.temperature;
    llama_config.n_predict = config.max_tokens;       // max_tokens → n_predict
    return std::make_unique<LlamaAdapterProvider>(llama_config);
  }
};

LLMProviderFactory::LLMProviderFactory()
    : mock_factory(std::make_unique<MockProviderFactory>()),
      cloud_factory(std::make_unique<CloudProviderFactory>()),
      llama_factory(std::make_unique<LlamaProviderFactory>()) {}

bool LLMProviderFactory::is_reserved_backend(const std::string& name) {
  return name == "mock" || name == "openai" || name == "anthropic" ||
         name == "deepseek" || name == "minimax" || name == "qwen" ||
         name == "moonshot" || name == "custom" || name == "local" ||
         name == "llama";
}

bool LLMProviderFactory::register_dynamic(std::string name,
                                          DynamicFactoryFn factory_fn) {
  if (name.empty() || !static_cast<bool>(factory_fn)) return false;
  if (is_reserved_backend(name)) return false;
  std::unique_lock lock(dynamic_mutex_);
  if (dynamic_factories_.count(name)) return false;
  dynamic_factories_.emplace(std::move(name), std::move(factory_fn));
  return true;
}

bool LLMProviderFactory::switch_default(const std::string& name) {
  std::unique_lock lock(dynamic_mutex_);
  if (!dynamic_factories_.count(name)) return false;
  default_provider_ = name;
  return true;
}

std::string LLMProviderFactory::current_default() const {
  std::shared_lock lock(dynamic_mutex_);
  return default_provider_;
}

bool LLMProviderFactory::has_dynamic(const std::string& name) const {
  std::shared_lock lock(dynamic_mutex_);
  return dynamic_factories_.count(name) > 0;
}

std::vector<std::string> LLMProviderFactory::dynamic_names() const {
  std::shared_lock lock(dynamic_mutex_);
  std::vector<std::string> out;
  out.reserve(dynamic_factories_.size());
  for (const auto& [k, _] : dynamic_factories_) out.push_back(k);
  return out;
}

std::unique_ptr<ILLMProvider> LLMProviderFactory::create(const LLMConfig& config) {
  std::string backend = config.provider;
  DynamicFactoryFn dynamic_factory;
  {
    std::shared_lock<std::shared_mutex> lock(dynamic_mutex_);
    if (backend.empty()) backend = default_provider_;
    auto it = dynamic_factories_.find(backend);
    if (it != dynamic_factories_.end()) dynamic_factory = it->second;
  }
  if (dynamic_factory) return dynamic_factory(config);  // construct outside lock

  if (backend == "mock" || backend.empty()) return mock_factory->create(config);
  if (backend == "openai" || backend == "anthropic" || backend == "deepseek" ||
      backend == "minimax" || backend == "qwen" || backend == "moonshot" ||
      backend == "custom") {
    // Wave 1 #2 (fix-cloud-adapter-multithreading): cloud 路径注入 SerializingDecorator
    // 修复 N≥2 worker 并发 + Authorization + https 三者同时触发的 SIGSEGV.
    // factory 层注入, 调用方无感知 (返回类型仍为 unique_ptr<ILLMProvider>).
    // mock / llama 路径不包装 — 零性能影响 (B.3/B.4 mock 测试不变).
    // ⚠️ NOT redundant: 牺牲并发 LLM 调用换取零 SIGSEGV. 真根因修复 (OpenSSL 3.0
    // + httplib 升级) 见 follow-up ADR-0087.
    auto adapter = cloud_factory->create(config);
    return std::make_unique<SerializingDecorator>(
        std::move(adapter), "cloud-" + backend);
  }
  if (backend == "local" || backend == "llama") return llama_factory->create(config);
  return mock_factory->create(config);
}

}  // namespace agenticdsl
