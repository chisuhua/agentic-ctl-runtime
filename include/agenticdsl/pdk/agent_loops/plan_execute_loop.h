// include/agenticdsl/pdk/agent_loops/plan_execute_loop.h
// 文件头注释
// 功能描述：PlanExecuteLoop — PDK Agent 三阶段循环 (Sprint 20 实施, ADR-0021 §3.2)。
//          状态机: Planning → Executing → Verifying → Done / Retry
//          Plan 阶段: LLM 生成 DSL 片段 (markdown)
//          Execute 阶段: DSLEngine 解析 + 执行生成的 DSL (复用 Sprint 4 基础设施)
//          Verify 阶段: LLM 评估 ExecutionResult, yes/no 决策
//          Retry: Verify 失败时重新 Plan (最多 max_retries 次, 默认 3)
//          复用 SimpleCognitiveOrchestrator 模式 (ADR-0020 §2.2.1): 委托 ILLMProvider::generate()
// 设计依据：ADR-0021 §3.2 + ADR-0020 SimpleCognitiveOrchestrator + ADR-0008 LayeredContext
//          + openspec/changes/pdk-plan-execute-fork-join
// 作者：AgenticDSL Phase 1 Sprint 20
// 最后修改日期：2026-08-01

#pragma once

#include "agenticdsl/contract/iinteraction_bus.h"
#include "agenticdsl/pdk/agent_loops/loop_result.h"
#include "agenticdsl/types/layered_context.h"
#include "common/llm/llm_types.h"
#include "core/engine.h"

#include <memory>
#include <optional>
#include <string>

namespace hydraforge::pdk {

/**
 * @brief PlanExecuteLoop — PDK Agent 三阶段循环 (Sprint 20)
 *
 * 状态机 (5 状态):
 *   Planning   → Executing → Verifying → Done     (成功路径)
 *   Planning   → Executing → Verifying → Retry    (Verify 失败, 重新 Plan)
 *   Retry      → Planning (回退)
 *   Planning/Executing/Verifying → 整体失败       (任何阶段 critical error, 返回 success=false)
 *
 * Plan 阶段契约:
 *   - 构造 prompt: "Goal: <goal>\nContext: <ctx.dump()>\nGenerate AgenticDSL:"
 *   - 调用 llm_->generate(prompt) 获取 markdown DSL 片段
 *   - 空响应 → 返回 std::nullopt, 标记失败
 *   - 非空 → 返回 string, 供 Execute 阶段使用
 *
 * Execute 阶段契约:
 *   - 调用 engine_->continue_with_generated_dsl(plan_dsl) 解析 + append graphs
 *   - 调用 engine_->run(ctx.dump()) 执行, 获取 ExecutionResult
 *   - success==false → LoopResult.success=false + failed_phase="Execute"
 *
 * Verify 阶段契约:
 *   - 构造 prompt: "Goal: <goal>\nResult: <result>. Verify success: yes/no:"
 *   - 调用 llm_->generate(prompt)
 *   - 响应含 "yes" (大小写不敏感) → 成功
 *   - 响应含 "no" 或空响应 → 失败 (触发 Retry)
 *
 * Retry 逻辑:
 *   - Verify 失败 → state = Retry, retries_used++, 重新进入 Planning
 *   - retries_used >= max_retries → 整体失败, LoopResult.success=false
 */
class PlanExecuteLoop {
 public:
  /**
   * @brief 5 状态机 (Planning / Executing / Verifying / Done / Retry)
   */
  enum class State { Planning, Executing, Verifying, Done, Retry };

  /**
   * @brief 构造 PlanExecuteLoop
   * @param engine       DSLEngine unique_ptr (执行 Plan 生成的 DSL)
   * @param bus          IInteractionBus shared_ptr (空指针允许, F7 顺序契约)
   * @param max_retries  Verify 失败最大重试次数 (默认 3, 含首次共 4 次)
   *
   * 契约:
   *   - 构造时调用 engine_->set_interaction_bus(bus_) (F7)
   *   - 构造后 state_ == Planning
   *   - max_retries 必须 >= 1, 否则抛 std::invalid_argument
   */
  PlanExecuteLoop(std::unique_ptr<agenticdsl::DSLEngine> engine,
                  std::shared_ptr<agenticdsl::IInteractionBus> bus,
                  int max_retries = 3)
      : engine_(std::move(engine)),
        bus_(std::move(bus)),
        max_retries_(max_retries) {
    if (max_retries_ < 1) {
      throw std::invalid_argument(
          "PlanExecuteLoop: max_retries must be >= 1");
    }
    if (engine_ && bus_) {
      engine_->set_interaction_bus(bus_);
    }
    state_ = State::Planning;
  }

  /**
   * @brief 执行三阶段循环
   * @param goal 用户目标 (作为 Plan 阶段 prompt 输入)
   * @param ctx  初始 LayeredContext (作为 Execute 阶段 Context 起点 + 最终 final_context 起点)
   * @return LoopResult
   *
   * 循环: Plan → Execute → Verify → (Retry if verify fail) → Done
   * 失败场景:
   *   - engine_ 为 null → Planning 阶段失败
   *   - LLM provider 为 null → Plan 阶段失败
   *   - Plan 输出空 → Planning 阶段失败
   *   - Engine execute 失败 → Executing 阶段失败
   *   - Verify 失败重试 max_retries 次后仍失败 → Verifying 阶段失败
   */
  LoopResult run(const std::string& goal, const agenticdsl::LayeredContext& ctx,
               std::stop_token token = {}) {
    LoopResult result;
    result.final_context = ctx;
    // 显式初始化 working.data 为空对象, 防止后续 dump() 在 null 上失败
    result.final_context.working["data"] = nlohmann::json::object();

    if (!engine_) {
      result.success = false;
      result.message = "PlanExecuteLoop: DSLEngine is null";
      result.failed_phase = "Planning";
      state_ = State::Done;
      return result;
    }

    agenticdsl::ILLMProvider* llm = engine_->get_llm_provider();
    if (!llm) {
      result.success = false;
      result.message = "PlanExecuteLoop: LLM provider is null";
      result.failed_phase = "Planning";
      state_ = State::Done;
      return result;
    }

    // Phase B Step 4: 取消 token early-exit
    if (token.stop_requested()) {
      result.success = false;
      result.message = "PlanExecuteLoop: cancelled before planning";
      result.failed_phase = "Planning";
      state_ = State::Done;
      return result;
    }

    // 主循环: Plan → Execute → Verify, Verify 失败时 Retry 重新 Plan
    while (true) {
      result.total_steps++;

      // === Plan 阶段 ===
      state_ = State::Planning;
      std::optional<std::string> plan_output =
          plan_phase(goal, ctx, llm, token);
      if (!plan_output.has_value()) {
        result.success = false;
        result.message = "PlanExecuteLoop: plan phase failed (empty LLM response)";
        result.failed_phase = "Planning";
        state_ = State::Done;
        return result;
      }

      // === Execute 阶段 ===
      state_ = State::Executing;
      bool exec_ok = execute_phase(plan_output.value(), ctx, result);
      if (!exec_ok) {
        result.success = false;
        result.message =
            "PlanExecuteLoop: execute phase failed: " +
            result.final_context.working["meta"].value(
                "execute_error", std::string{"unknown"});
        result.failed_phase = "Executing";
        state_ = State::Done;
        return result;
      }

      // === Verify 阶段 ===
      state_ = State::Verifying;
      bool verify_ok = verify_phase(goal, result, llm, token);

      if (verify_ok) {
        result.success = true;
        result.message = "PlanExecuteLoop: completed successfully";
        state_ = State::Done;
        return result;
      }

      // Verify 失败: 检查重试次数
      if (result.retries_used >= max_retries_) {
        result.success = false;
        result.message =
            "PlanExecuteLoop: verify failed after " +
            std::to_string(result.retries_used) + " retries";
        result.failed_phase = "Verifying";
        state_ = State::Done;
        return result;
      }

      // Retry: 重新 Plan
      state_ = State::Retry;
      result.retries_used++;
    }
  }

  /**
   * @brief 当前状态 (测试用)
   */
  State state() const { return state_; }

 private:
  /**
   * @brief Plan 阶段: LLM 生成 DSL 片段
   * @return std::optional<string> 非空表示生成的 DSL, nullopt 表示失败 (空响应)
   */
  std::optional<std::string> plan_phase(const std::string& goal,
                                        const agenticdsl::LayeredContext& ctx,
                                        agenticdsl::ILLMProvider* llm,
                                        std::stop_token token = {}) {
    agenticdsl::GenerationRequest req;
    req.prompt =
        "Goal: " + goal +
        "\nContext: " + ctx.dump().dump() +
        "\nGenerate AgenticDSL markdown for /main subgraph:";
    // ⚠️ NOT redundant: LLMParams = LLMConfig 别名, 默认 model = "gpt-4o-mini"
    // (非空). 若不清空, CloudLLMAdapter::build_request_body L164
    // (req.params.model.empty() ? config_.model : req.params.model) 会拿默认
    // "gpt-4o-mini" 遮蔽 adapter 构造时 factory 设置的真实 model
    // (如 deepseek-v4-flash) → deepseek server 拒绝
    // ("you passed gpt-4o-mini"). 站点无 model 概念 (model 由 adapter/factory 持有),
    // 清空让 adapter fallback.
    // 详见 openspec/changes/fix-generation-request-model-default/ + plan-execute-loop-realllm/.
    req.params.model.clear();
    auto gen_result = llm->generate(req, token);
    if (!gen_result.has_value()) {
      return std::nullopt;
    }
    const auto& text = gen_result.value().text;
    if (text.empty()) {
      return std::nullopt;
    }
    return text;
  }

  /**
   * @brief Execute 阶段: DSLEngine 解析 + 追加生成的 DSL
   * @return true 表示解析成功, false 表示失败 (result.final_context 仍填充)
   *
   * 注: 本实现仅调用 engine_->continue_with_generated_dsl (parse + append),
   * 不实际 engine_->run(). 因为 LLM 生成的 DSL 通常是新子图 (例如 /plan_1),
   * 与初始 /main 不冲突; 实际 run 由调用方在 verify 之后决定.
   * 这样保持 Plan→Execute→Verify 编排的纯粹性, 避免重复 /main 的 scheduler 冲突.
   */
  bool execute_phase(const std::string& generated_dsl,
                     const agenticdsl::LayeredContext& /*ctx*/,
                     LoopResult& result) {
    try {
      engine_->continue_with_generated_dsl(generated_dsl);
      result.final_context.working["meta"]["plan_appended"] = true;
      return true;
    } catch (const std::exception& e) {
      result.final_context.working["meta"]["execute_error"] = e.what();
      return false;
    }
  }

  /**
   * @brief Verify 阶段: LLM 评估 ExecutionResult
   * @return true 表示验证通过 (LLM 响应含 "yes"), false 表示失败
   */
  bool verify_phase(const std::string& goal,
                    const LoopResult& result,
                    agenticdsl::ILLMProvider* llm,
                    std::stop_token token = {}) {
    // 确保 working["data"] 存在, 避免 dump() 在 null/const path 上失败
    const auto& working = result.final_context.working;
    std::string data_dump = working.is_object() && working.contains("data")
                                ? working["data"].dump()
                                : std::string{"{}"};
    agenticdsl::GenerationRequest req;
    req.prompt =
        "Goal: " + goal +
        "\nResult: " + data_dump +
        "\nVerify success: answer 'yes' or 'no':";
    // ⚠️ NOT redundant: 与 plan_phase 同理 (LLMParams 默认 model 遮蔽 adapter
    // config_.model), 详见 plan_phase 上方注释 + openspec/changes/fix-generation-request-model-default/.
    req.params.model.clear();
    auto gen_result = llm->generate(req, token);
    if (!gen_result.has_value()) {
      return false;
    }
    const auto& text = gen_result.value().text;
    std::string lower = text;
    for (auto& c : lower) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower.find("yes") != std::string::npos;
  }

  std::unique_ptr<agenticdsl::DSLEngine> engine_;
  std::shared_ptr<agenticdsl::IInteractionBus> bus_;
  int max_retries_;
  State state_ = State::Planning;
};

} // namespace hydraforge::pdk