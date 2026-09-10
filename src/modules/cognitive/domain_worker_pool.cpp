// src/modules/cognitive/domain_worker_pool.cpp
// 文件头注释
// 功能描述：DomainWorkerPool 完整实现 — 领域智能体工作线程池（ADR-0020 §3.2）。
//          N 个 std::jthread worker 共享 FIFO 任务队列, std::stop_token 协作式取消,
//          shared_mutex 保护 handler 注册表, handler 异常通过 try-catch + catch(...)
//          隔离, 通过可选 IInteractionBus 推送 domain.task.* 事件. 析构函数隐式 stop()
//          + join 所有 jthread (PIMPL-lite 模式, 同 CognitiveWorker TD-CW-02).
// 设计依据：ADR-0020 §2.2.1 (P2) + §3.2 (实施参考) + ADR-0019 + ADR-0023 P1-P4
//          + openspec/changes/2026-06-30-domain-worker-pool
// 作者：AgenticDSL Phase 1 Sprint 3
// 最后修改日期：2026-06-19

#include "agenticdsl/cognitive/domain_worker_pool.h"
#include "agenticdsl/contract/bus_event.h"
#include "agenticdsl/contract/evaluation_events.h"
#include "agenticdsl/contract/event_builder.h"
#include "agenticdsl/contract/ievaluator.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <exception>
#include <stdexcept>
#include <typeinfo>
#include <utility>

namespace agenticdsl {

// =====================================================================
// 构造: 校验 num_threads > 0, 初始化 bus_ 与 state_ (默认 idle)
// =====================================================================
DomainWorkerPool::DomainWorkerPool(std::size_t num_threads)
    : DomainWorkerPool(num_threads, nullptr) {
  // 委托给双参数构造 (nullptr bus_, MVP 向后兼容)
}

DomainWorkerPool::DomainWorkerPool(std::size_t num_threads,
                                   std::shared_ptr<IInteractionBus> bus)
    : num_threads_(num_threads), bus_(std::move(bus)) {
  if (num_threads_ == 0) {
    throw std::invalid_argument(
        "DomainWorkerPool: num_threads must be > 0");
  }
  // state_ 默认 idle (std::atomic 初始化)
  // threads_ 默认空 (start() 才 emplace_back)
  // queue_/handlers_ 默认空
}

// =====================================================================
// 析构: state_ == running 时隐式 stop() (TD-CW-02 模式, 防 std::terminate)
// 必须 out-of-line 定义, 因为 std::jthread + shared_mutex + unordered_map
// 析构需完整类型, 头文件仅前向声明即可
// =====================================================================
DomainWorkerPool::~DomainWorkerPool() {
  if (state_.load(std::memory_order_acquire) == State::running) {
    // 隐式 stop(): 内部已 request_stop + notify_all + join 所有 jthread
    stop();
  }
  // 隐式析构成员按声明逆序:
  //   next_worker_ / handlers_ / handlers_mutex_ / queue_ / queue_cv_ /
  //   queue_mutex_ / threads_ / bus_ / num_threads_
  // threads_ 析构时 jthread 自动 request_stop + join (C++20 std::jthread 析构行为)
  // —— 双重保险: 即使 stop() 未调用, jthread 析构也保证 joinable() == false
}

// =====================================================================
// start: state_ == idle -> running, 启动 N 个 std::jthread
// =====================================================================
void DomainWorkerPool::start() {
  State expected = State::idle;
  if (!state_.compare_exchange_strong(expected, State::running)) {
    throw std::logic_error(
        "DomainWorkerPool::start: invalid state (expected idle, got " +
        std::string(expected == State::running ? "running" : "stopped") + ")");
  }
  threads_.reserve(num_threads_);
  for (std::size_t i = 0; i < num_threads_; ++i) {
    // emplace_back jthread: callback 接受 std::stop_token 参数 (C++20 强制)
    threads_.emplace_back([this, i](std::stop_token st) {
      worker_loop(st, i);
    });
  }
}

// =====================================================================
// submit_task: 加锁入队 + notify_one, 立即返回
// =====================================================================
void DomainWorkerPool::submit_task(DomainTask task) {
  if (state_.load(std::memory_order_acquire) != State::running) {
    throw std::logic_error(
        "DomainWorkerPool::submit_task: invalid state (pool not running)");
  }
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    task_queue_.push(std::move(task));
  }
  queue_cv_.notify_one();  // 唤醒一个 worker (多消费者)
}

// =====================================================================
// stop: state_ == running -> stopped, 协作式取消 + join 所有 jthread
// =====================================================================
void DomainWorkerPool::stop() {
  State expected = State::running;
  if (!state_.compare_exchange_strong(expected, State::stopped)) {
    return;  // idle / stopped: 幂等 no-op
  }
  // 协作式取消: 先 request_stop (即使 worker 不在 wait, 退出 handler 后也会看到)
  for (auto& t : threads_) {
    if (t.joinable()) {
      t.request_stop();
    }
  }
  // 唤醒所有 worker 退出 condition_variable::wait
  queue_cv_.notify_all();
  // join 所有 jthread (等待 in-flight task 完成)
  for (auto& t : threads_) {
    if (t.joinable()) {
      t.join();
    }
  }
}

// =====================================================================
// set_evaluator: 可选注入 (ADR-0083), 默认 nullptr 不评估不发射事件
// =====================================================================
void DomainWorkerPool::set_evaluator(std::shared_ptr<IEvaluator> evaluator) {
  evaluator_ = std::move(evaluator);
}

// =====================================================================
// register_domain_handler: unique_lock(handlers_mutex_) 注册
// 重复注册抛 std::invalid_argument
// =====================================================================
void DomainWorkerPool::register_domain_handler(
    const std::string& domain,
    std::function<nlohmann::json(const DomainTask&)> handler) {
  if (!handler) {
    throw std::invalid_argument(
        "DomainWorkerPool::register_domain_handler: handler must be callable");
  }
  std::unique_lock<std::shared_mutex> write_lock(handlers_mutex_);
  auto [it, inserted] = handlers_.try_emplace(domain, std::move(handler));
  if (!inserted) {
    throw std::invalid_argument(
        "DomainWorkerPool::register_domain_handler: domain already registered: " +
        domain);
  }
  // inserted == true: 新注册成功
  // 注意: domain 不应为空, 但不强制 (允许空 domain 表示 "default" handler)
  (void)it;
}

// =====================================================================
// unregister_domain_handler: unique_lock(handlers_mutex_) 移除
// 未注册抛 std::out_of_range
// =====================================================================
void DomainWorkerPool::unregister_domain_handler(const std::string& domain) {
  std::unique_lock<std::shared_mutex> write_lock(handlers_mutex_);
  if (handlers_.erase(domain) == 0) {
    throw std::out_of_range(
        "DomainWorkerPool::unregister_domain_handler: domain not registered: " +
        domain);
  }
}

// =====================================================================
// worker_loop: 阻塞消费任务, 委托 process_task
// 队列排空策略: 优先消费队列, 当队列空且 state_ != running 时退出
// (保证 stop() 调用后 in-flight + 队列中的 task 全部处理完)
// =====================================================================
void DomainWorkerPool::worker_loop(std::stop_token st, std::size_t worker_id) {
  while (true) {
    DomainTask task;
    bool got_task = false;

    // 1) 阻塞等待 task / stop 唤醒
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock, [&] {
        return !task_queue_.empty() || st.stop_requested();
      });
      // 队列非空: 优先处理 task (无论 stop_token 与否)
      if (!task_queue_.empty()) {
        task = std::move(task_queue_.front());
        task_queue_.pop();
        got_task = true;
      } else {
        // 队列空 + stop_token: 协作式退出
        break;
      }
    }

    // 锁外处理 task (允许其他 worker 抢下一个)
    if (got_task) {
      process_task(worker_id, std::move(task));
    }
  }
}

// =====================================================================
// process_task: 异常隔离 + 事件发布
// 锁顺序: queue_mutex_ (出队时释放) -> handlers_mutex_ (查表, 释放) -> handler 调用
// =====================================================================
void DomainWorkerPool::process_task(std::size_t worker_id, DomainTask task) {
  // 1) 推送 domain.task.started 事件 (ADR-0068 §5.8: EventBuilder 链式构造)
  if (bus_) {
    auto builder = agenticdsl::EventBuilder("domain.task.started")
                       .args(nlohmann::json{
                           {"domain", task.domain},
                           {"tool_name", task.tool_name},
                           {"output_key", task.output_key}
                       })
                       .meta(nlohmann::json{{"worker_id", worker_id}});
    if (task.parent_trace.has_value()) {
      builder = builder.parent_trace(*task.parent_trace);
    }
    bus_->emit(builder.build());
  }

  // 2) 查表 + 拷贝 handler (在 shared_lock 下查, 释放锁后调用)
  std::function<nlohmann::json(const DomainTask&)> handler;
  {
    std::shared_lock<std::shared_mutex> read_lock(handlers_mutex_);
    auto it = handlers_.find(task.domain);
    if (it == handlers_.end()) {
      // 域未注册: 推 failed 事件, worker 继续
      read_lock.unlock();  // 显式释放, 推事件时不持锁

      ToolResult failed;
      failed.ok = false;
      failed.error_code = ErrorCode::Unknown;
      failed.meta["error_message"] = "no handler for domain: " + task.domain;
      failed.meta["domain"] = task.domain;
      failed.meta["tool_name"] = task.tool_name;
      failed.meta["output_key"] = task.output_key;
      failed.meta["worker_id"] = worker_id;
      if (bus_) {
        // ADR-0068 §决策 7: operation-result event 通过 EventBuilder 接管 7 字段
        // (promote-event-builder-fulltoolresult-support 2026-08-03 V2 扩展)
        bus_->emit(EventBuilder("domain.task.failed", failed).build());
      }
      return;
    }
    handler = it->second;  // 拷贝 std::function (值类型)
    // read_lock 作用域结束自动释放
  }

  // 3) 调用 handler (异常隔离 — MUST 在 handlers_mutex_ 释放后调用)
  ToolResult result;
  result.meta["domain"] = task.domain;
  result.meta["tool_name"] = task.tool_name;
  result.meta["output_key"] = task.output_key;
  result.meta["worker_id"] = worker_id;

  // ADR-0037 L2: parent_trace 透传至 emit 事件 payload (顶层 ToolResult 字段)
  if (task.parent_trace.has_value()) {
    result.parent_trace = *task.parent_trace;
  }

  try {
    nlohmann::json output = handler(task);
    result.ok = true;
    result.data[task.output_key] = std::move(output);
  } catch (const std::exception& e) {
    // 标准异常: 填充 error_code + error_message
    result.ok = false;
    result.error_code = ErrorCode::Unknown;
    result.meta["error_message"] = e.what();
  } catch (...) {
    // 非 std::exception 异常: catch(...) 兜底, 防止 std::terminate
    result.ok = false;
    result.error_code = ErrorCode::Unknown;
    result.meta["error_message"] = "unknown exception (non-std)";
  }

  // 4) 推 completed / failed 事件
  // ADR-0068 §决策 7: operation-result events 通过 EventBuilder 接管 7 字段
  // (promote-event-builder-fulltoolresult-support 2026-08-03 V2 扩展)
  if (bus_) {
    if (result.ok) {
      bus_->emit(EventBuilder("domain.task.completed", result).build());
    } else {
      bus_->emit(EventBuilder("domain.task.failed", result).build());
    }
  }

  // 5) ADR-0083: 可选评估 (evaluator 为 nullptr 时跳过, 不发射 evaluation.result)
  //    评估器异常不 kill worker (best-effort V1, try-catch 隔离, 与 handler 隔离同级)
  if (bus_ && evaluator_) {
    try {
      ExecutionTrace trace;
      trace.final_result = result;
      trace.trace_id = result.trace_id.value_or(task.tool_name);
      const RewardSignal signal = evaluator_->evaluate(trace);
      bus_->emit(evaluation::build_evaluation_result_event(
          typeid(*evaluator_).name(), trace, signal));
    } catch (...) {
      // best-effort: 评估失败不影响任务结果与 worker 生命周期
    }
  }

  // 6) 派发计数器递增 (调试用, atomic 保证线程安全)
  next_worker_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace agenticdsl
