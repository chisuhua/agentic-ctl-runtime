#ifndef AGENTICDSL_LLM_LLM_PROVIDER_FACTORY_H
#define AGENTICDSL_LLM_LLM_PROVIDER_FACTORY_H

#include "agenticdsl/contract/iprovider_factory.h"

#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace agenticdsl {

class LLMProviderFactory : public IProviderFactory {
 public:
  using DynamicFactoryFn =
      std::function<std::unique_ptr<ILLMProvider>(const LLMConfig&)>;

  // ADR-0087 root cause 升级后, cloud 路径默认无 SerializingDecorator (OPT-IN 降级开关)
  struct CreateOptions {
    bool serializer = false;
  };

  LLMProviderFactory();
  ~LLMProviderFactory() override = default;

  std::unique_ptr<ILLMProvider> create(const LLMConfig& config) override;

  std::unique_ptr<ILLMProvider> create(const LLMConfig& config,
                                       const CreateOptions& opts);

  bool register_dynamic(std::string name, DynamicFactoryFn factory_fn);
  bool switch_default(const std::string& name);
  std::string current_default() const;
  bool has_dynamic(const std::string& name) const;
  std::vector<std::string> dynamic_names() const;

 private:
  static bool is_reserved_backend(const std::string& name);

  std::unique_ptr<IProviderFactory> mock_factory;
  std::unique_ptr<IProviderFactory> cloud_factory;
  std::unique_ptr<IProviderFactory> llama_factory;

  mutable std::shared_mutex dynamic_mutex_;
  std::unordered_map<std::string, DynamicFactoryFn> dynamic_factories_;
  std::string default_provider_;
};

}  // namespace agenticdsl

#endif
