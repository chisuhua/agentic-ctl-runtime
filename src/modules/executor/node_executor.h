// modules/executor/include/executor/node_executor.h
// P1.T4 (2026-06-18): ToolRegistry& → IToolRegistry& (依赖倒置, ADR-0019 §1.4 解耦)
// Sprint 19 (OpenSpec change pimpl-node-executor-h):
//   ApprovalHandler* → IApprovalHandler* (依赖抽象, ADR-0019 §1.4 + ADR-0031 §决策 5)
#ifndef AGENTICDSL_MODULES_EXECUTOR_NODE_EXECUTOR_H
#define AGENTICDSL_MODULES_EXECUTOR_NODE_EXECUTOR_H

#include "core/types/context.h" // 引入 Context
#include "core/types/node.h"    // 引入 NodePath, Node, NodeType, StartNode, EndNode, etc.
#include "core/types/resource.h" // 引入 ResourceType
#include "common/utils/template_renderer.h" // 引入 InjaTemplateRenderer
// P1.T4: 改为 IToolRegistry 抽象 (不再拖入 common/tools/registry.h)
#include "agenticdsl/contract/itool_registry.h" // P1.T2: IToolRegistry 抽象接口
#include "agenticdsl/contract/iparser.h" // ADR-0019 §1.4: 仅依赖解析器抽象接口
#include "agenticdsl/contract/iinteraction_bus.h" // Phase 1 Sprint 1b (S1b.T3): IInteractionBus 事件推送契约 (ADR-0019 P2)
#include "agenticdsl/contract/i_stream_handle.h" // ADR-0072 D1 阶段 B: IStreamHandle L1 契约层
// Sprint 19: 改为 IApprovalHandler 抽象 (不再拖入 common/policy/approval_handler.h)
#include "agenticdsl/policy/iapproval_handler.h" // Sprint 19: 审批处理器抽象 (ADR-0019 §1.4 解耦)
#include <nlohmann/json.hpp>
#include <atomic>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>

namespace agenticdsl {

// C₁.2: 前向声明 ILLMProvider（避免 llm_tool.h 的 LLMParams struct 与 llm_types.h 的 alias 冲突）
class ILLMProvider;

using BudgetChecker = std::function<bool()>;
inline BudgetChecker default_budget_checker() { return []() { return true; }; }

// C4 Sprint 14 (ADR-0031 P3-P4, Oracle ses_0ed4408faffeLv8VfrC0s5PzW7): 前向声明 ToolCoordinator
class ToolCoordinator;

// C10 Phase 5 Stage 1 Step 0: 前向声明 ExecutionSession (module_state 持久化)
class ExecutionSession;

using AppendGraphsCallback = std::function<void(std::vector<ParsedGraph>)>;

class NodeExecutor {
public:
    // C₁.2 迁移：构造函数从 LlamaAdapter* 改为 ILLMProvider*（向后兼容：仍可传 nullptr）
    // Phase 1 Sprint 1b (S1b.T3): 新增 IInteractionBus* 可选注入参数（默认 nullptr 保持现有 11 测试零修改）
    // P1.T4 (2026-06-18): ToolRegistry& → IToolRegistry& (依赖倒置)
    NodeExecutor(IToolRegistry& tool_registry, ILLMProvider* llm_provider = nullptr,
                 IInteractionBus* bus = nullptr);

    // ADR-0019 §1.4 + Stage 4 Task 20: 依赖反转，注入 IParser 抽象
    // Phase 1 Sprint 1b (S1b.T3): 新增 IInteractionBus* 可选注入参数（默认 nullptr）
    // P1.T4: ToolRegistry& → IToolRegistry&
    NodeExecutor(IToolRegistry& tool_registry, ILLMProvider* llm_provider,
                 std::unique_ptr<IParser> parser,
                 IInteractionBus* bus = nullptr);

    // 执行一个节点，返回新的上下文
    // C12 §6: budget_checker 默认 noop，仅 YIELD CONTINUE 模式真正使用
    Context execute_node(Node* node, const Context& ctx, BudgetChecker budget_checker = default_budget_checker(),
                        std::stop_token token = {});
    void set_append_graphs_callback(AppendGraphsCallback cb) {
        append_graphs_callback_ = std::move(cb);
    }

    // ADR-0031 (2026-07-31): 注入审批处理器（nullptr 跳过审批）
    // C4 Sprint 14 (Oracle ses_0ed4408faffeLv8VfrC0s5PzW7): 已废弃 — C4 起改用 set_tool_coordinator
    // Sprint 19: 参数类型 ApprovalHandler* → IApprovalHandler* (依赖抽象, ADR-0019 §1.4)
    [[deprecated("use set_tool_coordinator(ToolCoordinator*)")]]
    void set_approval_handler(IApprovalHandler* handler) { approval_handler_ = handler; }

    void set_session(ExecutionSession* session) { session_ = session; }

    // C4 Sprint 14 (Oracle ses_0ed4408faffeLv8VfrC0s5PzW7): 设置 ToolCoordinator
    // 优先级: tool_coordinator_ > approval_handler_ > direct call_tool()
    void set_tool_coordinator(ToolCoordinator* coordinator) {
      tool_coordinator_ = coordinator;
    }

    // ADR-0072 D1 阶段 B: stream 注入点 (raw pointer, no ownership, 与 set_tool_coordinator 一致)
    void set_stream_sink(IStreamHandle* sink) { stream_sink_ = sink; }

private:
    // P1.T4: IToolRegistry& (依赖倒置, 通过 has_tool/call_tool/call_llm_tool 多态分派)
    IToolRegistry& tool_registry_;
    // C₁.2: ILLMProvider 接口注入点（可为 nullptr）
    ILLMProvider* llm_provider_;
    AppendGraphsCallback append_graphs_callback_;
    // ADR-0019 §1.4 + Stage 4 Task 20: 通过 IParser 抽象持有具体解析器
    std::unique_ptr<IParser> parser_;

    // Phase 1 Sprint 1b (S1b.T3): 非 owning 指针（生命周期短于 DSLEngine）；
    // 默认 nullptr 走原有静默路径，保持现有 11+ 测试零回归。
    IInteractionBus* bus_;

    ExecutionSession* session_{nullptr};

    // ADR-0031 (2026-07-31): 可选审批处理器（nullptr 表示跳过审批）
    // Sprint 19: ApprovalHandler* → IApprovalHandler* (依赖抽象, ADR-0019 §1.4)
    IApprovalHandler* approval_handler_{nullptr};
    std::atomic<size_t> tool_call_count_{0}; // 本 session 工具调用计数 (TSan: atomic 防 data race)

    // C4 Sprint 14 (Oracle ses_0ed4408faffeLv8VfrC0s5PzW7): ToolCoordinator 优先于 approval_handler_
    ToolCoordinator* tool_coordinator_{nullptr};

    // ADR-0072 D1 阶段 B: stream 注入点 (raw pointer, no ownership)
    IStreamHandle* stream_sink_{nullptr};
    static constexpr size_t kStreamChunkSize = 64;

    // 权限检查
    void check_permissions(const std::vector<std::string>& perms, const NodePath& node_path);

    // 内部执行方法，根据节点类型分发
    Context execute_start(const StartNode* node, const Context& ctx);
    Context execute_end(const EndNode* node, const Context& ctx);
    Context execute_assign(const AssignNode* node, const Context& ctx);
    // C₁.2: execute_llm_call 已删除（LLMCallNode 死代码，分发 switch 中无对应 case）
    Context execute_dsl_node(const DSLNode* node, const Context& ctx);
    Context execute_tool_call(const ToolCallNode* node, const Context& ctx);
    Context execute_resource(const ResourceNode* node, const Context& ctx);
    Context execute_generate_subgraph(const GenerateSubgraphNode* node, const Context& ctx);
    Context execute_assert(const AssertNode* node, const Context& ctx) ;
    // C12 Phase 5 Stage 1 Step 2 §3: YIELD/STREAM 节点执行 (NEXT/CONTINUE/STOP)
    Context execute_yield(const YieldNode* node, const Context& ctx, BudgetChecker budget_checker,
                         std::stop_token token = {});

    // Sprint 17 C.2: execute_tool_call helper methods
    bool handle_tool_errors(const ToolCallNode* node, const ToolResult& result);
    void process_output_keys(Context& new_context,
                              const std::vector<std::string>& output_keys,
                              const nlohmann::json& data);
    std::pair<ToolResult, Context> dispatch_to_tool(
        const std::string& tool_name, const std::string& node_path,
        const std::unordered_map<std::string, std::string>& args,
        std::stop_token token = {});
};

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_EXECUTOR_NODE_EXECUTOR_H
