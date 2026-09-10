// include/agenticdsl/cognitive/domain_worker_pool.h
// 文件头注释
// 功能描述：DomainWorkerPool — 领域智能体工作线程池（ADR-0020 §3.2）。
//          持有 N 个 std::jthread worker (默认 4), 共享 FIFO 任务队列 (多消费者),
//          共享领域处理器注册表 (std::shared_mutex 保护), 通过可选的 IInteractionBus
//          推送 domain.task.* 事件. Phase 1 Sprint 3 实施:
//            - 双构造重载: (num_threads) 与 (num_threads, shared_ptr<IInteractionBus>)
//            - 显式状态机 enum class State { idle, running, stopped }
//            - std::jthread 协作式取消 (std::stop_token + request_stop)
//            - 析构函数 out-of-line 定义, 隐式 stop() + join
//            - 头文件前向声明所有外部类型 (PIMPL-lite 模式, 避免引入 core/engine.h)
// 设计依据：ADR-0020 §2.2.1 (P2) + §3.2 (实施参考) + ADR-0019 IInteractionBus
//          + ADR-0023 ToolResult P1-P4 + openspec/changes/2026-06-30-domain-worker-pool
// 作者：AgenticDSL Phase 1 Sprint 3
// 最后修改日期：2026-06-19

#pragma once

#include "agenticdsl/contract/iinteraction_bus.h"
#include "core/types/tool_result.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace agenticdsl {

class IEvaluator;

/**
 * @brief 领域任务结构 (Sprint 3 MVP)
 *
 * 用于 DomainWorkerPool::submit_task() 提交一个领域任务, worker 通过查
 * handlers_["domain"] 路由到对应处理器.
 *
 * 字段:
 *  - domain:       领域标识 (e.g. "code", "browser", "fs")
 *  - tool_name:    工具全名 (e.g. "code::edit_file", 用于 bus 事件关联)
 *  - arguments:    工具参数 (nlohmann::json, handler 自由解析)
 *  - output_key:   handler 返回 json 写入 result.data[output_key] 的 key
 *  - parent_trace: ADR-0037 L2 因果链上游 task_id, 透传到 emit payload.parent_trace
 */
struct DomainTask {
  std::string domain;
  std::string tool_name;
  nlohmann::json arguments = nlohmann::json::object();
  std::string output_key;
  std::optional<std::string> parent_trace;  // ADR-0037 causal-ordering-completion
};

/**
 * @brief 领域智能体工作线程池 (Sprint 3)
 *
 * 设计要点 (ADR-0020 §2.2.1 P2 + §3.2):
 *  - N 个 std::jthread worker 共享同一 FIFO 任务队列 (多消费者模式, 无 dispatcher 线程)
 *  - 显式状态机: idle / running / stopped, 公开方法在 entry 处 assert 前置条件
 *  - 协作式取消: stop_token 优先于 notify_all (避免 worker 阻塞在 handler 调用)
 *  - 异常隔离: handler 抛任何异常 MUST NOT 导致 worker 退出 (try-catch + catch(...))
 *  - 析构函数 out-of-line 定义, state_ == running 时隐式 stop() + join
 *
 * 线程模型 (Sprint 3 MVP):
 *  - 共享 mutex: queue_mutex_ (FIFO 队列), handlers_mutex_ (注册表)
 *  - 锁顺序: queue_mutex_ 总是先于 handlers_mutex_ 获取 (CP.22 协议)
 *  - 持锁期间 MUST NOT 调用 handler() (避免持锁递归死锁)
 *
 * 事件 topic (Sprint 3 约定, 遵循 <module>.<verb> 模式):
 *  - "domain.task.started"    (ToolResult payload, meta 含 domain/tool_name/output_key/worker_id)
 *  - "domain.task.completed"  (ToolResult payload, data[output_key] + meta)
 *  - "domain.task.failed"     (ToolResult payload, error_code=ErrorCode::Unknown + meta.error_message)
 *
 * 异常安全: 公开方法不抛 std::exception (除 invalid_argument/logic_error 状态机违规);
 *           handler 异常由 process_task 内部 try-catch 捕获并通过 ToolResult 表达。
 */
class DomainWorkerPool {
 public:
  /**
   * @brief 构造 DomainWorkerPool (无 bus 版本, MVP 向后兼容)
   * @param num_threads worker 线程数 (默认 4, 必须 > 0)
   *
   * 契约:
   *  - 构造后 state_ == idle
   *  - 构造后 worker 线程未启动, 仅持有空 handlers_/queue
   *  - 调用 start() 后才创建 std::jthread
   *
   * 异常: 若 num_threads == 0, 抛 std::invalid_argument
   */
  explicit DomainWorkerPool(std::size_t num_threads = 4);

  /**
   * @brief 构造 DomainWorkerPool (带 bus 版本, Sprint 3 推荐)
   * @param num_threads worker 线程数 (默认 4, 必须 > 0)
   * @param bus        IInteractionBus 共享指针 (worker 事件 + future engine 事件统一转发)
   *
   * 契约: 同上, 仅多持有一个 bus 引用 (F7 顺序契约对齐 CognitiveWorker)
   */
  DomainWorkerPool(std::size_t num_threads,
                   std::shared_ptr<IInteractionBus> bus);

  /**
   * @brief 析构函数 (out-of-line, .cpp 中定义)
   *
   * 行为:
   *  - state_ == running: 隐式 stop() (内部已 request_stop + notify_all + join 所有 jthread)
   *  - state_ == idle:    no-op (worker 未启动, jthread 不可 join)
   *  - state_ == stopped: join 已退出 jthread (no-op)
   *
   * 重要: 头文件仅声明, .cpp 定义 — std::jthread + shared_mutex + unordered_map 析构需完整类型。
   * 重要: 头文件不引入 core/engine.h — 所有外部类型仅前向声明 (PIMPL-lite 模式)。
   */
  ~DomainWorkerPool();

  // 禁止拷贝/移动 (jthread + mutex + condition_variable 不易移动)
  DomainWorkerPool(const DomainWorkerPool&) = delete;
  DomainWorkerPool& operator=(const DomainWorkerPool&) = delete;
  DomainWorkerPool(DomainWorkerPool&&) = delete;
  DomainWorkerPool& operator=(DomainWorkerPool&&) = delete;

  /**
   * @brief 启动 worker 线程池
   *
   * 前置条件: state_ == idle
   * 违反: 抛 std::logic_error
   * 后置条件: state_ == running, N 个 jthread 阻塞在 condition_variable::wait
   */
  void start();

  /**
   * @brief 停止 worker 线程池 (协作式 + 等待 in-flight task 完成)
   *
   * 前置条件: state_ == running
   * 违反: no-op (state_ == idle/stopped 时幂等)
   * 后置条件: state_ == stopped, 所有 jthread 已 join
   */
  void stop();

  /**
   * @brief 提交一个领域任务 (异步, 非阻塞)
   * @param task DomainTask 任务结构 (含 domain/tool_name/arguments/output_key)
   *
   * 前置条件: state_ == running
   * 违反: 抛 std::logic_error
   *
   * 行为: 加锁入队 + notify_one, 立即返回。
   *       任意一个空闲 worker 抢到该 task 并开始处理。
   */
  void submit_task(DomainTask task);

  /**
   * @brief 注册领域处理器
   * @param domain  领域标识 (e.g. "code", "browser")
   * @param handler 处理器函数 (signature: nlohmann::json(const DomainTask&))
   *
   * 前置条件: state_ 可以是 idle/running (允许运行时注册)
   * 行为: unique_lock(handlers_mutex_) 下注册, 重复注册抛异常
   *
   * 异常:
   *  - handler 为空: 抛 std::invalid_argument
   *  - domain 已注册: 抛 std::invalid_argument("domain already registered: <name>")
   */
  void register_domain_handler(
      const std::string& domain,
      std::function<nlohmann::json(const DomainTask&)> handler);

  /**
   * @brief 取消注册领域处理器
   * @param domain 领域标识
   *
   * 前置条件: domain 必须已注册
   * 行为: unique_lock(handlers_mutex_) 下移除
   *
   * 异常:
   *  - domain 未注册: 抛 std::out_of_range
   */
  void unregister_domain_handler(const std::string& domain);

  /**
   * @brief 当前状态 (用于测试与诊断)
   */
  enum class State { idle, running, stopped };
  State state() const { return state_.load(std::memory_order_acquire); }

  /**
   * @brief worker 线程数 (构造时固定, 不可变)
   */
  std::size_t num_threads() const { return num_threads_; }

  /**
   * @brief 注入评估器 (可选, ADR-0083, 非构造注入)
   *
   * 契约:
   *  - evaluator == nullptr: 任务完成后不评估, 不发射 evaluation.result 事件
   *  - evaluator != nullptr 且 bus_ != nullptr: 任务完成后
   *    (domain.task.completed/failed 之后) 调用 evaluator->evaluate(trace)
   *    并发射 evaluation.result 事件
   *  - 线程模型 V1: 假定在 start() 前单线程调用 (design D4), 无 mutex
   */
  void set_evaluator(std::shared_ptr<IEvaluator> evaluator);

 private:
  /**
   * @brief Worker 主循环 (在 std::jthread 内运行)
   * @param st         std::stop_token (协作式取消信号)
   * @param worker_id  worker 编号 (0..N-1, 用于 bus 事件 payload meta.worker_id 调试)
   *
   * 行为:
   *  - while (!st.stop_requested()) 阻塞等待任务
   *  - condition_variable::wait 唤醒后, 检查 stop_requested 优先 (协作式取消)
   *  - 抢到 task 后, 调用 process_task() 处理 (try-catch 异常隔离)
   *  - 循环直到 stop_requested
   */
  void worker_loop(std::stop_token st, std::size_t worker_id);

  /**
   * @brief 处理单个 task (异常隔离, 事件发布)
   * @param worker_id worker 编号 (bus 事件调试用)
   * @param task      待处理任务 (已从队列出队, 移所有权)
   *
   * 行为:
   *  1. 推送 domain.task.started 事件
   *  2. shared_lock(handlers_mutex_) 查表, 拷贝 handler callable
   *  3. 释放锁后调用 handler (异常隔离 try-catch + catch(...))
   *  4. 推 domain.task.completed (ok) 或 domain.task.failed (!ok) 事件
   *
   * 锁顺序: queue_mutex_ 总是先于 handlers_mutex_ (CP.22 协议)
   *         handler MUST 在释放 handlers_mutex_ 后调用 (避免持锁递归)
   */
  void process_task(std::size_t worker_id, DomainTask task);

  // === 不可变 (构造时固定) ===
  std::size_t num_threads_;
  std::shared_ptr<IInteractionBus> bus_;
  std::shared_ptr<IEvaluator> evaluator_;  // 可选, 默认 nullptr (ADR-0083)

  // === 生命周期 (start/stop) ===
  std::vector<std::jthread> threads_;
  std::atomic<State> state_{State::idle};

  // === 共享任务队列 (FIFO, 多消费者) ===
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::queue<DomainTask> task_queue_;

  // === 领域处理器注册表 (shared_mutex 保护, 多读少写) ===
  std::shared_mutex handlers_mutex_;
  std::unordered_map<std::string,
      std::function<nlohmann::json(const DomainTask&)>> handlers_;

  // === 派发计数器 (调试用, atomic 保证线程安全) ===
  std::atomic<std::size_t> next_worker_{0};
};

} // namespace agenticdsl
