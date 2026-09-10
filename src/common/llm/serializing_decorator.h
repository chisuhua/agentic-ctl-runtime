// serializing_decorator.h
// 文件头注释
// 功能描述：SerializingDecorator — 串行化 ILLMProvider 装饰器
//          修复 CloudLLMAdapter 多线程 SIGSEGV bug (gdb 已实证)
//          根因: N≥2 worker 并发 + Authorization header + https 三者同时
//          → httplib::Client::Post create_client_socket 栈 corruption
//          修复: 工厂层为 cloud 路径注入 SerializingDecorator,
//          用 std::mutex + std::condition_variable 串行化 generate/generate_stream
//          ADR-0087 Step 4 (Sprint 27): 升级后改为 OPT-IN 降级开关 (默认不注入,
//          opts.serializer = true 显式启用, 见 LLMProviderFactory::CreateOptions)
// 设计依据：openspec/changes/fix-cloud-adapter-multithreading/design.md
//          + openspec/changes/adr-0087-root-cause-upgrade/design.md §Decision 3
// 作者：AgenticDSL Wave 1 #2 (post-real-llm-core-coverage Phase B SIGSEGV)
// 最后修改日期：2026-09-11

#ifndef AGENTICDSL_LLM_SERIALIZING_DECORATOR_H
#define AGENTICDSL_LLM_SERIALIZING_DECORATOR_H

#include "common/llm/llm_types.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

namespace agenticdsl {

/**
 * @brief 串行化装饰器 — 修复 CloudLLMAdapter 多线程 SIGSEGV (Wave 1 #2)
 *
 * 不继承 ILLMProviderDecorator 因为基类标记 generate() / generate_stream() 为
 * final, 而本装饰器需要控制 inner call 时机 (等待 cv + 调用 + 释放).
 * 直接继承 ILLMProvider; 仍可被其他装饰器 (CostTracking/Compliance) 通过
 * wrap_chain() 包装 (wrap_chain 接受 ILLMProvider 而非 ILLMProviderDecorator).
 *
 * 部署位置: LLMProviderFactory::create("deepseek"/"openai"/...) 包装 cloud
 * adapter 后立即注入; mock/llama 路径不包装 (零影响).
 *
 * 行为 (mutex + cv 串行化):
 *  - generate(): 等待 cv (concurrent_count_ == 0), 唤醒后调用 inner_,
 *    返回前减并发计数 + notify_all
*  - generate_stream(): 同 generate() 语义, 串行化获取 inner stream,
 *    流本身不持锁 (否则会持有锁 10000ms 阻塞后续 waiter)
 *  - stop_token: cv_.wait 谓词包含 token.stop_requested(), 取消时:
 *      - generate()      → 立即返回 LLMError{Code::Cancelled}
 *      - generate_stream() → 立即返回 nullptr (调用方需检查)
 *
 * ⚠️ NOT redundant: 牺牲并发 LLM 调用换取零 SIGSEGV. 真根因修复 (OpenSSL 3.0
 * + httplib 升级) 见 follow-up ADR-0087. 当前实现是规避 (serialization),
 * 不是根因修复.
 *
 * 线程安全: 全部状态 (concurrent_count_, total_serialize_waiters_, mutex_, cv_)
 *          仅在 mutex_ 保护下访问; stop_token.stop_requested() 是 lock-free
 *          atomic 检查.
 *
 * 性能 trade-off:
 *  - 单 worker: 无影响 (无并发, 锁直接获得)
 *  - N worker 并发: N× 串行 (N × 单调用延迟); mock 路径零影响 (不包装)
 *  - Worker 池非 LLM 工作: 仍并发 (LLM 调用串行只影响 LLM 路径)
 */
class SerializingDecorator : public ILLMProvider {
 public:
  /**
   * @brief 构造串行化装饰器
   * @param inner 被包装的 ILLMProvider (owned)
   * @param purpose 用途标签 (诊断用, e.g. "cloud-deepseek")
   */
  SerializingDecorator(std::unique_ptr<ILLMProvider> inner,
                       std::string purpose = "default");

  ~SerializingDecorator() override = default;

  // 禁止拷贝 (mutex_ 不可拷贝)
  SerializingDecorator(const SerializingDecorator&) = delete;
  SerializingDecorator& operator=(const SerializingDecorator&) = delete;

  // === ILLMProvider 实现 ===

  /// 串行化同步 generate
  /// - 等待 cv (concurrent_count_ == 0 OR token cancelled)
  /// - 唤醒后调 inner_->generate() 并返回 result
  Result<GenerationResult, LLMError> generate(
      const GenerationRequest& req, std::stop_token token) override;

  /// 串行化流式 generate_stream (获取 inner stream 时串行, 流本身不持锁)
  std::unique_ptr<IGenerationStream> generate_stream(
      const GenerationRequest& req, std::stop_token token) override;

  /// 透传 inner_->available_models() (无需串行, 查询方法)
  std::vector<ILLMProvider::ModelInfo> available_models() const override;

  // === 诊断接口 (测试用) ===

  /// 当前正在调用 inner 的并发数 (应为 0 或 1, 单线程锁内)
  int concurrent_count() const;

  /// 当前正在 cv 上等待的线程数 (transient, 锁释放瞬间可能变化)
  std::size_t active_waiters() const;

  /// 历史累计进入 cv.wait 的线程数 (诊断: 验证确实有并发请求被串行化)
  std::size_t cumulative_waiters() const;

  /// 装饰器用途标签
  const std::string& purpose() const { return purpose_; }

 private:
  std::unique_ptr<ILLMProvider> inner_;
  std::string purpose_;

  // 串行化状态
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  int concurrent_count_ = 0;            // 当前持有 inner 的调用数 (应为 0 或 1)
  std::atomic<std::size_t> active_waiters_{0};           // 当前正在 cv 上等待的线程数 (transient)
  std::atomic<std::size_t> cumulative_waiters_{0};       // 历史累计进入 cv.wait 的线程数 (诊断)
};

}  // namespace agenticdsl

#endif  // AGENTICDSL_LLM_SERIALIZING_DECORATOR_H