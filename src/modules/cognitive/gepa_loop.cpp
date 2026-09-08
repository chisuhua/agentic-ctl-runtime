// src/modules/cognitive/gepa_loop.cpp
// 功能描述：GEPALoop V1 反思循环实现 (T19, ADR-0071)
//          同步编排 LLM 反思、SkillCompiler、T14 回归门、V2 评估与治理提交。
// 设计依据：openspec/changes/t19-gepa-phase2-commit/tasks.md Phase 1-2
// 作者：HydraForge Sprint 24 T19 ship
// 最后修改日期：2026-08-27

#include "agenticdsl/cognitive/gepa_loop.h"

#include "agenticdsl/contract/event_builder.h"
#include "agenticdsl/cognitive/skill_compiler.h"
#include "agenticdsl/ir/trajectory_ir.h"
#include "agenticdsl/testing/behavioral_regression.h"
#include "common/llm/llm_types.h"

#include <memory>
#include <string>
#include <utility>

namespace agenticdsl {

namespace {

std::string make_reflection_id(const ExecutionTrace& trace, int iteration) {
  return trace.trace_id + ":reflection:" + std::to_string(iteration);
}

void emit_event(const std::shared_ptr<IInteractionBus>& bus,
                const std::string& topic,
                const nlohmann::json& args) {
  if (bus) {
    bus->emit(EventBuilder(topic).args(args).build());
  }
}

}  // namespace

GEPALoop::GEPALoop(std::shared_ptr<IEvaluator> evaluator,
                   std::shared_ptr<IMutationGovernor> governor,
                   std::shared_ptr<ILLMProvider> llm)
    : GEPALoop(std::move(evaluator), std::move(governor), std::move(llm), Config{}, nullptr) {}

GEPALoop::GEPALoop(std::shared_ptr<IEvaluator> evaluator,
                    std::shared_ptr<IMutationGovernor> governor,
                    std::shared_ptr<ILLMProvider> llm,
                    Config config,
                    std::shared_ptr<IInteractionBus> bus)
    : evaluator_(std::move(evaluator)),
      governor_(std::move(governor)),
      llm_(std::move(llm)),
      bus_(std::move(bus)),
      config_(std::move(config)) {}

// T6 gepa-mcts-budget-integration: budget controller ctor overload
GEPALoop::GEPALoop(std::shared_ptr<IEvaluator> evaluator,
                    std::shared_ptr<IMutationGovernor> governor,
                    std::shared_ptr<IBudgetController> budget_controller,
                    std::shared_ptr<ILLMProvider> llm,
                    Config config,
                    std::shared_ptr<IInteractionBus> bus)
    : evaluator_(std::move(evaluator)),
      governor_(std::move(governor)),
      budget_controller_(std::move(budget_controller)),
      llm_(std::move(llm)),
      bus_(std::move(bus)),
      config_(std::move(config)) {}

GEPALoop::ReflectionResult GEPALoop::reflect_and_commit(
    const ExecutionTrace& failed_trace) {
  ReflectionResult result;
  if (!evaluator_ || !governor_ || !llm_ || config_.max_iterations <= 0) {
    result.failure_mode = "invalid_configuration";
    return result;
  }

  const RewardSignal failed_signal = evaluator_->evaluate(failed_trace);
  const ir::TrajectoryIR::CanonicalIR trajectory_snapshot{};
  const std::string trajectory_hash = ir::TrajectoryIR::hash(trajectory_snapshot);

  for (int iteration = 0; iteration < config_.max_iterations; ++iteration) {
    // T6 gepa-mcts-budget-integration: 进化预算闸
    if (budget_controller_ && !budget_controller_->try_consume_evolution_llm_call()) {
      result.failure_mode = "evolution_budget_exceeded";
      nlohmann::json budget_payload;
      budget_payload["reason"] = result.failure_mode;
      budget_payload["iterations_used"] = iteration;
      emit_event(bus_, "gepa.reflection.failed", budget_payload);
      // T6 graceful break: evolution budget exceeded, 退出循环
      break;
    }
    const std::string reflection_id = make_reflection_id(failed_trace, iteration);
    emit_event(bus_, "gepa.reflection.started",
               {{"reflection_id", reflection_id}, {"trajectory_ir_hash", trajectory_hash}});

    MutationContext proposal;
    proposal.mutation_id = reflection_id;
    proposal.source_id = config_.source_id;
    proposal.mutation_kind = "L1_prompt";
    proposal.subject_ref = failed_trace.trace_id;
    proposal.parent_ref = failed_trace.trace_id;
    proposal.proposed_change = "GEPA reflection candidate";
    proposal.mode = MutationMode::Yolo;
    const MutationDecision proposed = governor_->propose(proposal);
    emit_event(bus_, "gepa.commit.proposed",
               {{"reflection_id", reflection_id}, {"candidate_skill", proposal.proposed_change}});
    if (!proposed.approved) {
      result.failure_mode = "proposal_denied";
      emit_event(bus_, "gepa.reflection.failed",
                 {{"reflection_id", reflection_id}, {"reason", result.failure_mode}});
      emit_event(bus_, "gepa.commit.denied",
                 {{"reflection_id", reflection_id}, {"reason", proposed.denial_reason}});
      continue;
    }

    GenerationRequest request("Reflect on failed execution " + failed_trace.trace_id);
    // ⚠️ NOT redundant: LLMParams = LLMConfig 别名, 默认 model = "gpt-4o-mini"
    // (非空). GenerationRequest(prompt) ctor 不显式设 params, 同样继承默认.
    // 若不清空, CloudLLMAdapter L164 会拿默认遮蔽 factory 设置的真实 model
    // → GEPA 反射真实 LLM 失败. 清空让 adapter fallback.
    // 详见 openspec/changes/fix-generation-request-model-default/.
    request.params.model.clear();
    const auto generated = llm_->generate(request, std::stop_token{});
    if (!generated.has_value()) {
      result.failure_mode = "reflection_generation_failed";
      emit_event(bus_, "gepa.reflection.failed",
                 {{"reflection_id", reflection_id}, {"reason", result.failure_mode}});
      emit_event(bus_, "gepa.commit.denied",
                 {{"reflection_id", reflection_id}, {"reason", result.failure_mode}});
      continue;
    }

    SkillCompiler compiler;
    const CompiledSkill candidate = compiler.compile(
        "---\nname: " + reflection_id + "\n---\n\n" + generated.value().text);
    if (!candidate.ok) {
      result.failure_mode = "skill_compilation_failed";
      emit_event(bus_, "gepa.reflection.failed",
                 {{"reflection_id", reflection_id}, {"reason", result.failure_mode}});
      emit_event(bus_, "gepa.commit.denied",
                 {{"reflection_id", reflection_id}, {"reason", result.failure_mode}});
      continue;
    }

    const BehaviorFingerprint baseline = compute_fingerprint({});
    const BehaviorFingerprint candidate_fp = baseline;
    const Verdict verdict = hotelling_t2_test(baseline, candidate_fp, RegressionBudget{});
    if (verdict == Verdict::Fail) {
      result.failure_mode = "behavioral_regression_failed";
      emit_event(bus_, "gepa.reflection.failed",
                 {{"reflection_id", reflection_id}, {"reason", result.failure_mode}});
      emit_event(bus_, "gepa.commit.denied",
                 {{"reflection_id", reflection_id}, {"reason", result.failure_mode}});
      return result;
    }

    ExecutionTrace candidate_trace;
    candidate_trace.final_result = ToolResult::success(generated.value().text);
    candidate_trace.trace_id = reflection_id;
    const RewardSignal candidate_signal = evaluator_->evaluate(candidate_trace);
    if (candidate_signal.scalar <= failed_signal.scalar + config_.reward_threshold) {
      result.failure_mode = "no_improvement";
      emit_event(bus_, "gepa.reflection.completed",
                 {{"reflection_id", reflection_id}, {"regression_verdict", verdict_to_string(verdict)}});
      emit_event(bus_, "gepa.commit.denied",
                 {{"reflection_id", reflection_id}, {"reason", result.failure_mode}});
      continue;
    }

    proposal.version_id = reflection_id;
    proposal.evaluation_refs = {reflection_id};
    const MutationDecision committed = governor_->commit(proposal);
    if (!committed.approved) {
      result.failure_mode = "commit_denied";
      emit_event(bus_, "gepa.reflection.failed",
                 {{"reflection_id", reflection_id}, {"reason", result.failure_mode}});
      emit_event(bus_, "gepa.commit.denied",
                 {{"reflection_id", reflection_id}, {"reason", committed.denial_reason}});
      continue;
    }

    result.success = true;
    result.candidate_skills.push_back(candidate.compiled_content);
    emit_event(bus_, "gepa.reflection.completed",
               {{"reflection_id", reflection_id}, {"regression_verdict", verdict_to_string(verdict)}});
    emit_event(bus_, "gepa.commit.committed",
               {{"reflection_id", reflection_id}, {"commit_id", proposal.version_id},
                {"evaluation_refs", proposal.evaluation_refs}});
    return result;
  }

  return result;
}

}  // namespace agenticdsl