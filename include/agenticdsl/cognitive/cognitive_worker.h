// include/agenticdsl/cognitive/cognitive_worker.h
// 文件头注释
// 功能描述：CognitiveWorker — 认知智能体工作线程（ADR-0020 §3.1）。
//          持有独立 DSLEngine 实例 + IInteractionBus 共享指针 (per-agent 隔离)，
//          单线程处理任务队列，事件通过 IInteractionBus 转发。
//          Phase 1 Sprint 2 实施：
//            - 构造签名 (unique_ptr<DSLEngine>, shared_ptr<IInteractionBus>)
//              (amends ADR-0020 §3.1 line 199, 适配 from_markdown 工厂模式)
//            - 构造时强制调用 engine_->set_interaction_bus(bus_) (F7 ordering)
//            - 显式状态机 enum class State { idle, running, stopped }
//            - 析构函数 out-of-line 定义, 隐式 stop()+join (TD-CW-02 修复)
//            - 头文件前向声明 class DSLEngine; 避免引入 core/engine.h
// 设计依据：ADR-0020 §2.2.1 + §3.1 (amended) + ADR-0019 IInteractionBus
//          + openspec/changes/2026-06-23-cognitive-worker
// 作者：AgenticDSL Phase 1 Sprint 2
// 最后修改日期：2026-06-18

#pragma once

#include "agenticdsl/contract/iinteraction_bus.h"
#include "core/types/tool_result.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <tuple>
#include <utility>

namespace agenticdsl {

// 前向声明 — 避免在头文件中引入 core/engine.h (TD-CW-05 风险规避)。
// unique_ptr<DSLEngine> 的析构在 cognitive_worker.cpp 中需要 DSLEngine 完整类型，
// 由 out-of-line 析构函数 + 类外 ctor/dtor 共同保证 PIMPL-lite 风格。
class DSLEngine;
class IEvaluator;

/**
 * @brief 认知智能体工作线程（Sprint 2 MVP）
 *
 * 设计要点：
 *  - 每 CognitiveWorker 独占一个 DSLEngine 实例 (per-agent 隔离, ADR-0020 §2.2)
 *  - 单线程消费任务队列 (std::queue + std::mutex + std::condition_variable)
 *  - 显式状态机: idle / running / stopped, 公开方法的前置条件在 entry 处 assert
 *  - 事件通过 IInteractionBus 转发, 公开 topic:
 *      - "cognitive.task.started"    (ToolResult payload, meta["task_id"] 关联)
 *      - "cognitive.task.completed"  (ToolResult payload, trace_id = task_id)
 *  - 析构函数: state_ == running 时隐式 stop() + join (TD-CW-02 修复)
 *
 * 异常安全: 公开方法不抛 std::exception (除 logic_error 状态机违规);
 *           LLM/工具异常由 SimpleCognitiveOrchestrator 内部捕获并通过 ToolResult 表达。
 */
class CognitiveWorker {
 public:
  /**
   * @brief 构造 CognitiveWorker
   * @param engine  独立 DSLEngine 实例 (per-agent 隔离, 由 from_markdown/from_file 工厂创建)
   * @param bus     IInteractionBus 共享指针 (Worker 事件 + engine 事件统一通过此 bus 转发)
   *
   * 契约：
   *  - 构造时立即调用 engine_->set_interaction_bus(bus_), 保证 F7 顺序
   *  - 构造后 state_ == idle
   *  - 析构在 cognitive_worker.cpp 中 out-of-line 定义 (unique_ptr<DSLEngine> 析构)
   *
   * 异常: 若 engine/bus 为 nullptr, 抛出 std::invalid_argument
   */
  CognitiveWorker(std::unique_ptr<DSLEngine> engine,
                  std::shared_ptr<IInteractionBus> bus);

  /**
   * @brief 析构函数 (out-of-line, .cpp 中定义)
   *
   * 行为：
   *  - state_ == running: 隐式 stop() (内部 join thread) + 析构成员
   *  - state_ == idle:    no-op (thread 不可 join)
   *  - state_ == stopped: join 已退出 thread (no-op) + 析构成员
   *
   * 重要: 头文件仅声明, .cpp 定义 — unique_ptr<DSLEngine> 析构需完整类型。
   * 重要: 头文件不引入 core/engine.h — DSLEngine 仅前向声明。
   */
  ~CognitiveWorker();

  // 禁止拷贝/移动 (thread + mutex + condition_variable 不易移动)
  CognitiveWorker(const CognitiveWorker&) = delete;
  CognitiveWorker& operator=(const CognitiveWorker&) = delete;
  CognitiveWorker(CognitiveWorker&&) = delete;
  CognitiveWorker& operator=(CognitiveWorker&&) = delete;

  /**
   * @brief 启动 Worker 线程
   *
   * 前置条件: state_ == idle
   * 违反: 抛 std::logic_error
   * 后置条件: state_ == running
   */
  void start();

  /**
   * @brief 提交一个任务（异步, 非阻塞）
   * @param task_id 调用方提供的不透明关联键 (透传到 ToolResult::trace_id, P3 字段)
   * @param prompt  用户提示 (透传到 SimpleCognitiveOrchestrator::process)
   * @param parent_trace 可选因果链上游 task_id (ADR-0037 L2, 透传到 ToolResult::parent_trace)
   *                     默认 nullopt 保持既有 2 参数调用方零迁移。
   *
   * 前置条件: state_ == running
   * 违反: 抛 std::logic_error
   *
   * 行为: 加锁入队 + notify_one, 立即返回。
   */
  void submit_task(const std::string& task_id,
                   const std::string& prompt,
                   std::optional<std::string> parent_trace = std::nullopt);

  /**
   * @brief 停止 Worker (清理 thread, 取消未完成 task)
   *
   * 前置条件: state_ == running
   * 违反: no-op (state_ == idle/stopped 时幂等)
   * 后置条件: state_ == stopped
   */
  void stop();

  /**
   * @brief 当前状态（用于测试与诊断）
   */
  enum class State { idle, running, stopped };
  State state() const { return state_.load(); }

  /**
   * @brief 注入评估器 (可选, ADR-0083, 非构造注入)
   *
   * 契约:
   *  - evaluator == nullptr: 任务完成后不评估, 不发射 evaluation.result 事件
   *  - evaluator != nullptr: 任务完成后 (cognitive.task.completed 之后)
   *    调用 evaluator->evaluate(trace) 并发射 evaluation.result 事件
   *  - 线程模型 V1: 假定在 start() 前单线程调用 (design D4), 无 mutex
   */
  void set_evaluator(std::shared_ptr<IEvaluator> evaluator);

 private:
  /**
   * @brief Worker 主循环（在 std::thread 内运行）
   *
   * 行为:
   *  - 阻塞等待 condition_variable
   *  - 唤醒后出队一个 (task_id, prompt)
   *  - 推送 cognitive.task.started 事件
   *  - 调用 SimpleCognitiveOrchestrator 同步执行单轮 ReAct
   *  - bridge: meta["error_code"] 字符串 → ErrorCode enum (TD-CW-03)
   *  - 设置 result.trace_id = task_id (P3 字段, ADR-0023)
   *  - 推送 cognitive.task.completed 事件
   *  - 循环直到 state_ != running
   */
  void worker_loop();

  // 成员声明顺序决定析构顺序: thread 优先于 queue/cv/mutex, engine/bus 最后。
  std::atomic<State> state_{State::idle};
  std::unique_ptr<DSLEngine> engine_;  // 前向声明, .cpp 析构
  std::shared_ptr<IInteractionBus> bus_;
  std::shared_ptr<IEvaluator> evaluator_;  // 可选, 默认 nullptr (ADR-0083)
  std::thread worker_thread_;
  std::queue<std::tuple<std::string, std::string, std::optional<std::string>>>
      task_queue_;  // (task_id, prompt, parent_trace) — causal-ordering-completion
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
};

} // namespace agenticdsl
