// modules/executor/src/node_executor.cpp
#include "executor/node_executor.h"
#include "agenticdsl/executor/signature_validator.h"
#include "common/llm/llm_types.h" // C₁.2: 需要完整定义（生成 GenerationRequest/ILLMProvider）
#include "common/log/log.h"  // agenticdsl::log facade
#include "common/utils/template_renderer.h" // 引入 InjaTemplateRenderer (for rendering)
#include "core/types/tool_result.h" // Phase 1 Sprint 1a (S1a.T2): ToolResult envelope parsing (ADR-0023)
#include "modules/parser/markdown_parser.h" // Stage 4 Task 20: 仅在 .cpp 内用于默认 shim，不再被 header 引入
#include "scheduler/execution_session.h" // C12 §4 + §6.0: BudgetExceededException (executor 不调用 session_ 方法, 避免链接循环)
#include "yield_stream_bridge.h" // C12 §6a: IGenerationStream pull-based bridge
#include <stdexcept>
#include <stop_token>
#include <inja/inja.hpp> // For RenderError
#include <algorithm> // For std::find
#include <thread> // For std::this_thread::sleep_for (if needed for mock)
#include <chrono> // For std::chrono_literals + std::chrono::steady_clock
#include <string>
#include "agenticdsl/contract/bus_event.h" // BusEvent 统一事件信封
#include "agenticdsl/contract/event_builder.h" // ADR-0068 EventBuilder 链式构造

// C4 Sprint 14 (ADR-0031 P3-P4, Oracle ses_0ed4408faffeLv8VfrC0s5PzW7): ToolCoordinator
#include "common/tools/tool_coordinator.h"

namespace agenticdsl {

// C₁.2 迁移：构造函数从 LlamaAdapter* 改为 ILLMProvider*
// Stage 4 Task 20: 此构造函数保留为 shim，内部创建默认 MarkdownParser 并委托给主构造函数
// Phase 1 Sprint 1b (S1b.T3): 透传 bus 参数（默认 nullptr 走原有静默路径）
// P1.T4 (2026-06-18): ToolRegistry& → IToolRegistry& (依赖倒置)
NodeExecutor::NodeExecutor(IToolRegistry& tool_registry, ILLMProvider* llm_provider,
                           IInteractionBus* bus)
    : NodeExecutor(tool_registry, llm_provider,
                   std::make_unique<MarkdownParser>(), bus) {
    // llm_provider_ 可能为 nullptr，NodeExecutor 需要处理这种情况
}

// delegate constructor — 完整 5 参数
// Phase 1 Sprint 1b (S1b.T3): 接收 bus 参数（非 owning），存为 bus_ 成员
// P1.T4: ToolRegistry& → IToolRegistry&
NodeExecutor::NodeExecutor(IToolRegistry& tool_registry, ILLMProvider* llm_provider,
                            std::unique_ptr<IParser> parser, IInteractionBus* bus)
    : tool_registry_(tool_registry), llm_provider_(llm_provider),
      parser_(std::move(parser)), bus_(bus), session_(nullptr) {}


Context NodeExecutor::execute_node(Node* node, const Context& ctx, BudgetChecker budget_checker,
                                   std::stop_token token) {
    Context context_with_resources = ctx;

    check_permissions(node->permissions, node->path);

    switch (node->type) {
        case NodeType::START:
            return execute_start(dynamic_cast<const StartNode*>(node), context_with_resources);
        case NodeType::END:
            return execute_end(dynamic_cast<const EndNode*>(node), context_with_resources);
        case NodeType::ASSIGN:
            return execute_assign(dynamic_cast<const AssignNode*>(node), context_with_resources);
        case NodeType::DSL_CALL:
            return execute_dsl_node(dynamic_cast<const DSLNode*>(node), context_with_resources);
        case NodeType::TOOL_CALL:
            return execute_tool_call(dynamic_cast<const ToolCallNode*>(node), context_with_resources);
        case NodeType::RESOURCE:
            return execute_resource(dynamic_cast<const ResourceNode*>(node), context_with_resources);
        case NodeType::FORK:
        case NodeType::JOIN:
            throw std::logic_error("ForkNode/JoinNode reached NodeExecutor - scheduler routing bug");
        case NodeType::GENERATE_SUBGRAPH:
            return execute_generate_subgraph(dynamic_cast<const GenerateSubgraphNode*>(node), context_with_resources);
        case NodeType::ASSERT:
            return execute_assert(dynamic_cast<const AssertNode*>(node), context_with_resources);
        case NodeType::YIELD:
            return execute_yield(dynamic_cast<const YieldNode*>(node), context_with_resources, budget_checker, token);
        default:
            throw std::runtime_error("Unknown node type during execution: " + std::to_string(static_cast<int>(node->type)));
    }
}

void NodeExecutor::check_permissions(const std::vector<std::string>& perms, const NodePath& node_path) {
    // 简单实现：检查是否有权限要求，如果有，假设都需要（实际应用中需更细粒度）
    for (const auto& perm : perms) {
        // Example: "tool: web_search"
        if (perm.substr(0, 5) == "tool:") {
            std::string tool_name = perm.substr(6); // Extract name after "tool: "
            if (!tool_registry_.has_tool(tool_name)) {
                throw std::runtime_error("Permission denied: Tool '" + tool_name + "' not available for node: " + node_path);
            }
        }
        // Add more permission checks as needed (network, file, etc.)
    }
    // 例如，检查 perms 中是否包含 "network" 或 "tool: web_search" 等
    // 这里只是示例，实际权限检查逻辑会更复杂
}

Context NodeExecutor::execute_start(const StartNode* node, const Context& ctx) {
    // Start 节点通常不修改上下文，直接返回
    return ctx;
}

Context NodeExecutor::execute_end(const EndNode* node, const Context& ctx) {
    // End 节点通常不修改上下文，直接返回
    // 其终止逻辑由 TopoScheduler 处理
    return ctx;
}

Context NodeExecutor::execute_assign(const AssignNode* node, const Context& ctx) {
    Context new_context = ctx;
    for (const auto& [key, template_str] : node->assign) {
        try {
            std::string rendered_value = InjaTemplateRenderer::render(template_str, ctx);
            new_context[key] = rendered_value; // 赋值到新的上下文
        } catch (const inja::RenderError& e) {
            throw std::runtime_error("Template rendering failed for key '" + key + "': " + std::string(e.what()));
        }
    }
    return new_context;
}

Context NodeExecutor::execute_dsl_node(const DSLNode* node, const Context& ctx) {
    Context new_context = ctx;

    // ADR-0072 D1 阶段 B: stream 分支 (V1 切片回放, per Oracle B3 诚实化语义)
    // 触发条件: metadata["stream"] 严格 JSON bool true AND stream_sink_ 已注入
    bool stream_requested = node->metadata.is_object() &&
        node->metadata.contains("stream") &&
        node->metadata["stream"].is_boolean() &&
        node->metadata["stream"].get<bool>();

    if (stream_requested && stream_sink_ == nullptr) {
        std::cerr << "[WARN] stream:true ignored: no sink registered for node: "
                  << node->path << std::endl;
    }

    if (stream_requested && stream_sink_ != nullptr && !node->output_keys.empty()) {
        // V1: 内联同步路径 (避免新增 execute_dsl_node_sync 重复声明)
        // 走完后切片 key 输出值至 sink, 然后 close(nullopt) 标记 EOF
        if (ctx.contains(node->output_keys[0])) {
            new_context[node->output_keys[0]] = ctx[node->output_keys[0]];
        } else {
            std::string rendered_prompt = InjaTemplateRenderer::render(node->prompt_template, ctx);
            nlohmann::json result = tool_registry_.call_llm_tool(
                node->llm_tool_name, rendered_prompt, node->llm_params);
            if (!result.value("success", false)) {
                throw std::runtime_error("LLM generation failed: " +
                    result.value("error", "Unknown error"));
            }
            new_context[node->output_keys[0]] = result["text"].get<std::string>();
        }
        // 切片输出值推送至 sink
        std::string text = new_context[node->output_keys[0]].dump();
        for (size_t offset = 0; offset < text.size(); offset += kStreamChunkSize) {
            stream_sink_->push(text.substr(offset,
                std::min(kStreamChunkSize, text.size() - offset)));
        }
        stream_sink_->close(std::nullopt);
        return new_context;
    }

    if (node->output_keys.empty()) {
        throw std::runtime_error("DSLNode has no output_keys: " + node->path);
    }
    const std::string& key = node->output_keys[0];

    // If context already supplies a value for this output_key, treat it as a mock and skip LLM call
    if (ctx.contains(key)) {
        new_context[key] = ctx[key];
        return new_context;
    }

    // Phase 1 Sprint 1b (S1b.T3): 入口推送 dsl.call.started 事件 (REQ-BUS-003 Scenario)
    if (bus_) {
        bus_->emit(agenticdsl::EventBuilder("dsl.call.started")
            .args(nlohmann::json{
                {"node_path", node->path},
                {"llm_tool_name", node->llm_tool_name}
            })
            .meta(nlohmann::json{{"prompt", ""}})
            .build());  // 实际 prompt 在渲染后再次推送更准确；started 仅透出元信息
    }

    std::string rendered_prompt;
    try {
        // Render prompt template
        rendered_prompt = InjaTemplateRenderer::render(node->prompt_template, ctx);

        // Call LLM via ToolRegistry
        nlohmann::json result = tool_registry_.call_llm_tool(node->llm_tool_name, rendered_prompt, node->llm_params);

        // Check result
        if (!result.value("success", false)) {
            throw std::runtime_error("LLM generation failed: " + result.value("error", "Unknown error"));
        }

        new_context[key] = result["text"].get<std::string>();

        // Phase 1 Sprint 1b (S1b.T3): 成功退出时推送 dsl.call.completed 事件
        if (bus_) {
            bus_->emit(agenticdsl::EventBuilder("dsl.call.completed")
                .args(nlohmann::json{
                    {"node_path", node->path},
                    {"output_key", key}
                })
                .meta(nlohmann::json{{"text", new_context[key]}})
                .build());
        }

    } catch (const inja::RenderError& e) {
        throw std::runtime_error("Prompt template rendering failed for node '" + node->path + "': " + std::string(e.what()));
    }

    return new_context;
}

Context NodeExecutor::execute_tool_call(const ToolCallNode* node, const Context& ctx) {
    // ADR-0072 D1 阶段 B: stream 分支 (V1 切片回放, per Oracle B3 诚实化语义)
    bool stream_requested = node->metadata.is_object() &&
        node->metadata.contains("stream") &&
        node->metadata["stream"].is_boolean() &&
        node->metadata["stream"].get<bool>();

    if (stream_requested && stream_sink_ == nullptr) {
        std::cerr << "[WARN] stream:true ignored: no sink registered for node: "
                  << node->path << std::endl;
    }

    if (stream_requested && stream_sink_ != nullptr) {
        // V1: 内联同步路径, 完成后切片 tool_result.data 至 sink
        std::unordered_map<std::string, std::string> rendered_args;
        for (const auto& [key, tmpl] : node->arguments) {
            rendered_args[key] = InjaTemplateRenderer::render(tmpl, ctx);
        }
        if (!tool_registry_.has_tool(node->tool_name)) {
            throw std::runtime_error("Tool '" + node->tool_name + "' not registered for node: " + node->path);
        }
        auto [tool_result, new_context] = dispatch_to_tool(node->tool_name, node->path, rendered_args);
        new_context = ctx;
        if (!tool_result.ok && !handle_tool_errors(node, tool_result)) {
            return new_context;
        }
        process_output_keys(new_context, node->output_keys, tool_result.data);
        // 切片 tool_result.data.dump() 推送至 sink
        std::string text = tool_result.data.dump();
        for (size_t offset = 0; offset < text.size(); offset += kStreamChunkSize) {
            stream_sink_->push(text.substr(offset,
                std::min(kStreamChunkSize, text.size() - offset)));
        }
        stream_sink_->close(std::nullopt);
        return new_context;
    }

    // 渲染参数
    std::unordered_map<std::string, std::string> rendered_args;
    for (const auto& [key, tmpl] : node->arguments) {
        rendered_args[key] = InjaTemplateRenderer::render(tmpl, ctx);
    }

    if (!tool_registry_.has_tool(node->tool_name)) {
        throw std::runtime_error("Tool '" + node->tool_name + "' not registered for node: " + node->path);
    }

auto [tool_result, new_context] = dispatch_to_tool(node->tool_name, node->path, rendered_args);
    new_context = ctx;
    if (!tool_result.ok && !handle_tool_errors(node, tool_result)) {
        return new_context;  // Skip: 不处理 output_keys, 不 bus emit
    }
    process_output_keys(new_context, node->output_keys, tool_result.data);

    if (bus_) {
        // tool.completed 携带 ToolResult 整包（含 latency_ms/trace_id/error_code 等 optional 字段）
        // ADR-0068 §决策 7: operation-result event 通过 EventBuilder 接管 7 字段全透传
        // (promote-event-builder-fulltoolresult-support 2026-08-03 V2 扩展)
        bus_->emit(EventBuilder("tool.completed", tool_result).build());
    }

    return new_context;
}

Context NodeExecutor::execute_resource(const ResourceNode* node, const Context& ctx) {
    /*
    // 创建 Resource 对象并注册
    Resource resource;
    resource.path = node->path;
    resource.resource_type = node->resource_type;
    resource.uri = node->uri;
    resource.scope = node->scope;
    resource.metadata = node->metadata; // Use node's metadata

    ResourceManager::instance().register_resource(resource);
        */

    // Resource 节点通常不修改上下文，直接返回
    return ctx;
}

Context NodeExecutor::execute_assert(const AssertNode* node, const Context& ctx) {
    // Render the condition expression using the current context
    std::string rendered_condition_str;
    try {
        rendered_condition_str = InjaTemplateRenderer::render(node->condition, ctx);
    } catch (const inja::InjaError& e) {
        throw std::runtime_error("Assert condition template rendering failed for node '" + node->path + "': " + std::string(e.message));
    }

    // Convert rendered result to boolean
    // Inja renders to string, so we check string value
    bool condition_result = false;
    if (rendered_condition_str == "true") {
        condition_result = true;
    } else if (rendered_condition_str == "false") {
        condition_result = false;
    } else {
        // If the rendered result is not explicitly "true" or "false",
        // try to interpret it as a number (0 is false, non-zero is true)
        try {
            double num_val = std::stod(rendered_condition_str);
            condition_result = (num_val != 0.0);
    } catch (...) {
            // If it's not a number either, treat as false or throw error
            // Let's throw an error for non-boolean results
            throw std::runtime_error("Assert condition did not evaluate to a boolean value ('true'/'false' or number): " + rendered_condition_str);
        }
    }

    if (!condition_result) {
        // Condition failed
        if (node->on_failure.has_value()) {
            // This requires the scheduler to handle jumps.
            // For now, throw an error indicating the jump path.
            // The scheduler will catch this and handle the jump.
            throw std::runtime_error("Assert failed. Jumping to: " + node->on_failure.value());
        } else {
            // No jump path, just fail the execution
            throw std::runtime_error("Assert failed at node: " + node->path);
        }
    }
    // Condition passed, context remains unchanged
    return ctx;
}

Context NodeExecutor::execute_generate_subgraph(const GenerateSubgraphNode* node, const Context& ctx) {
    Context new_context = ctx;
    try {
        if (!ctx.contains("__rendered_prompt__")) {
            throw std::runtime_error("Missing __rendered_prompt__ in context for GenerateSubgraphNode");
        }
        std::string rendered_prompt = ctx.at("__rendered_prompt__").get<std::string>();
        // Add budget info to prompt context if needed by LLM
        // [DEBUG-removed] Context prompt_ctx = ctx;
        // [DEBUG-removed] prompt_ctx["available_subgraphs"] = PromptBuilder::build_available_libraries_context();
         // Add budget info (nodes_left, depth_left, etc.) - This requires access to ExecutionSession's budget
         // [DEBUG-removed] For now, assume budget info is added by the calling context or PromptBuilder
         // [DEBUG-removed] prompt_ctx["budget"] = ...; // Access budget from ExecutionSession

        // 2. Call LLM（C₁.2: 使用 ILLMProvider 接口，Result<T,E> 风格）
        std::string generated_dsl;
        if (llm_provider_) {
            GenerationRequest req;
            req.prompt = rendered_prompt;
            // ⚠️ NOT redundant: LLMParams = LLMConfig 别名, 默认 model = "gpt-4o-mini"
            // (非空). 若不清空, CloudLLMAdapter::build_request_body L164
            // (req.params.model.empty() ? config_.model : req.params.model)
            // 会拿默认 "gpt-4o-mini" 遮蔽 adapter 构造时 factory 设置的真实 model
            // (如 deepseek-v4-flash) → deepseek server 拒绝
            // ("you passed gpt-4o-mini"). 站点无 model 概念, 清空让 adapter fallback.
            // 实测: real-llm-core-coverage Phase A A.2 测试中加此行后 PASS.
            // 详见 openspec/changes/fix-generation-request-model-default/.
            req.params.model.clear();
            auto result = llm_provider_->generate(req, {});
            if (!result.has_value()) {
                throw std::runtime_error("LLM provider error: " + result.error().message);
            }
            generated_dsl = std::move(result.value().text);
        } else {
            throw std::runtime_error("LLM provider not available for generate_subgraph");
        }

        // 3. Parse LLM output for `### AgenticDSL '/dynamic/...'` blocks
        // This requires access to the main engine's parser and graph storage mechanism.
        // A reference or callback to the engine might be needed here, or the parsing result is returned.
        // For this executor, assume a global or injected mechanism or return the result for the scheduler to handle.
        // Let's assume the scheduler calls a parser and handles the dynamic graph registration.
        // Here, we just parse and return the generated paths in the context.
        // Stage 4 Task 20: 通过 IParser 抽象调用；IParser::parse 返回单 ParsedGraph，包装为 vector 以兼容下游 for-range
        // 注意：ParsedGraph 禁止拷贝（仅可移动），故不能使用 initializer_list 构造；改用 push_back 走移动路径
        std::vector<ParsedGraph> new_graphs;
        {
            auto* md_parser = dynamic_cast<MarkdownParser*>(parser_.get());
            if (md_parser) {
                new_graphs = md_parser->parse_from_string(generated_dsl);
            } else {
                new_graphs.push_back(parser_->parse(generated_dsl));
            }
        }
        std::vector<std::string> dynamic_paths; // Collect paths of generated graphs
        for (auto& graph : new_graphs) {
            if (graph.path.rfind("/dynamic/", 0) == 0) { // Ensure it's dynamic
                // 4. Validate signature if present (T4 signature-validation-real-impl)
                if (graph.signature.has_value()) {
                    using agenticdsl::executor::SignatureMode;
                    using agenticdsl::executor::SignatureValidator;
                    SignatureMode mode = SignatureMode::Strict;
                    if (node->signature_validation == "warn") mode = SignatureMode::Warn;
                    else if (node->signature_validation == "ignore") mode = SignatureMode::Ignore;

                    SignatureValidator validator(mode);
                    bool is_valid = true;
                    try {
                        validator.parse_signature_ast(graph.signature.value());
                    } catch (const std::exception& e) {
                        is_valid = false;
                        if (node->on_signature_violation.has_value()) {
                            throw std::runtime_error("Signature violation: " + std::string(e.what())
                                + " (jump target: " + node->on_signature_violation.value() + " not yet supported)");
                        }
                        throw std::runtime_error("GenerateSubGraphNode execution failed: " + std::string(e.what()));
                    }
                    if (!is_valid && mode == SignatureMode::Warn) {
                        LOG_WARN("Signature validation warning (already thrown in strict mode)");
                    }
                }
                dynamic_paths.push_back(graph.path);
                if (append_graphs_callback_) {
                    try {
                        std::vector<agenticdsl::ParsedGraph> to_register;
                        to_register.push_back(std::move(graph));
                        append_graphs_callback_(std::move(to_register));
                    } catch (const std::exception& cb_err) {
                        LOG_ERROR("GenerateSubGraphNode: append_graphs_callback_ threw for graph '"
                                  << graph.path << "': " << cb_err.what());
                    }
                } else {
                    LOG_WARN("GenerateSubGraphNode: append_graphs_callback_ not set, graph '"
                             << graph.path << "' dropped (call ExecutionSession::set_append_graphs_callback first)");
                }
            }
        }

        // 6. Store generated graph path(s) in context
        if (!node->output_keys.empty()) {
            if (dynamic_paths.size() == 1) {
                new_context[node->output_keys[0]] = dynamic_paths[0];
            } else {
                new_context[node->output_keys[0]] = dynamic_paths; // Store as array if multiple
            }
        }

        // 7. Snapshot trigger happens in ExecutionSession::execute_node, not here.

    } catch (const std::exception & e) {
        throw std::runtime_error("GenerateSubgraphNode execution failed: " + std::string(e.what()));
    }
    return new_context;
}

// Sprint 17 C.2: execute_tool_call helper methods

std::pair<ToolResult, Context> NodeExecutor::dispatch_to_tool(
    const std::string& tool_name, const std::string& node_path,
    const std::unordered_map<std::string, std::string>& args,
    std::stop_token token) {
  ToolMetadata meta;
  meta.name = tool_name;
  meta.category = ToolCategory::Execute;

  ToolCallContext tool_ctx;
  tool_ctx.call_count_this_session = tool_call_count_++;
  tool_ctx.target_path = node_path;

  ToolResult tool_result;
  const auto t0 = std::chrono::steady_clock::now();

  if (tool_coordinator_) {
    if (approval_handler_) {
      LOG_WARN("both tool_coordinator_ and approval_handler_ are set, preferring tool_coordinator_");
    }
    tool_result = tool_coordinator_->execute(meta, tool_ctx, args, token);
  } else if (approval_handler_) {
    ToolPreview preview;
    preview.command_line = tool_name;
    for (const auto& [k, v] : args) {
      preview.command_line += " " + k + "=" + v;
    }
    preview.risk_summary = "Tool: " + tool_name + " at " + node_path;

    if (!approval_handler_->process_request(meta, tool_ctx, preview)) {
      throw std::runtime_error("Tool '" + tool_name + "' denied by execution policy at node: " + node_path);
    }

    nlohmann::json raw_result = tool_registry_.call_tool(tool_name, args);
    tool_result = (raw_result.is_object() && raw_result.contains("ok") && raw_result["ok"].is_boolean())
        ? ToolResult::from_json(raw_result) : ToolResult::success(raw_result);
  } else {
    nlohmann::json raw_result = tool_registry_.call_tool(tool_name, args);
    tool_result = (raw_result.is_object() && raw_result.contains("ok") && raw_result["ok"].is_boolean())
        ? ToolResult::from_json(raw_result) : ToolResult::success(raw_result);
  }

  const auto t1 = std::chrono::steady_clock::now();
  tool_result.latency_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

  auto trace_it = args.find("trace_id");
  if (trace_it != args.end()) {
    tool_result.trace_id = trace_it->second;
  }

  return {std::move(tool_result), Context{}};
}

bool NodeExecutor::handle_tool_errors(const ToolCallNode* node, const ToolResult& result) {
  const ErrorCode code = result.error_code.value_or(ErrorCode::Unknown);
  const std::string err_msg = result.meta.value("error_message", std::string{});

  auto emit_failed = [&]() {
    if (bus_) {
      // execution.failed 携带 ok=false 的 ToolResult (含 error_code/latency_ms/trace_id)
      // ADR-0068 §决策 7: operation-result event 通过 EventBuilder 接管 7 字段全透传
      // (promote-event-builder-fulltoolresult-support 2026-08-03 V2 扩展)
      bus_->emit(EventBuilder("execution.failed",
                  ToolResult::error(code, err_msg,
                      {{"node_path", node->path}, {"tool_name", node->tool_name}}))
                  .build());
    }
  };

  switch (code) {
    case ErrorCode::Retry:
      emit_failed();
      throw std::runtime_error("[RETRY] Tool '" + node->tool_name + "': " + err_msg);
    case ErrorCode::Abort:
      emit_failed();
      throw std::runtime_error("[ABORT] Tool '" + node->tool_name + "': " + err_msg);
    case ErrorCode::Skip:
      return false;  // 无需额外处理
    default:
      emit_failed();
      throw std::runtime_error("Tool '" + node->tool_name + "' failed: " + err_msg);
  }
}

void NodeExecutor::process_output_keys(Context& new_context,
                                        const std::vector<std::string>& output_keys,
                                        const nlohmann::json& data) {
  if (output_keys.empty()) return;
  if (output_keys.size() == 1) {
    new_context[output_keys[0]] = data;
  } else if (data.is_object()) {
    for (const auto& key : output_keys) {
      if (data.contains(key)) new_context[key] = data[key];
    }
  } else {
    new_context[output_keys[0]] = data;
  }
}

// C12 Phase 5 Stage 1 Step 2 §3: YIELD/STREAM 节点执行 (NEXT/CONTINUE/STOP 三模式)
// 设计依据: IP-001 §Step 2 + Oracle Q3 决议 + spec.md `yield-execution-mode-support`
// 注意: pending_yield_ 状态由 ExecutionSession::execute_node 调用方维护 (避免 executor→scheduler 链接循环)
Context NodeExecutor::execute_yield(const YieldNode* node, const Context& ctx, BudgetChecker budget_checker,
                                    std::stop_token token) {
    Context new_context = ctx;

    std::string rendered;
    try {
        rendered = InjaTemplateRenderer::render(node->yield_value, ctx);
    } catch (const inja::RenderError& e) {
        throw std::runtime_error("YieldNode yield_value template rendering failed for node '" +
                                 node->path + "': " + std::string(e.what()));
    }

    if (llm_provider_ == nullptr) {
        return ctx;  // 静默跳过 (避免无 LLM provider 时崩溃, 保持向后兼容)
    }

    YieldStreamBridge bridge{};

    switch (node->mode) {
        case YieldMode::STOP: {
            new_context["__yield_mode__"] = "STOP";
            new_context["__yield_stop_path__"] = node->stop_path;
            break;
        }
        case YieldMode::NEXT:
        case YieldMode::CONTINUE: {
            GenerationRequest req;
            req.prompt = std::move(rendered);
            // ⚠️ NOT redundant: LLMParams = LLMConfig 别名, 默认 model = "gpt-4o-mini"
            // (非空). 若不清空, CloudLLMAdapter L164 会拿默认遮蔽 factory 配置的真实
            // model → server 拒绝. 站点无 model 概念, 清空让 adapter fallback.
            // 详见 openspec/changes/fix-generation-request-model-default/.
            req.params.model.clear();
            // fix-yield-node-token-passthrough ship 后: token 透传至 generate_stream,
            // 外部 cancel 可中断流式 LLM 调用 (Wave 1 #1 commit 5dc4569 标注的 GAP 修复).
            auto stream = llm_provider_->generate_stream(req, token);
            if (!stream) {
                new_context["__yield_error__"] = "null_stream";
                return new_context;
            }

            if (node->mode == YieldMode::NEXT) {
                auto token = bridge.pull_single(*stream);
                new_context["__yield__"] = token.value_or(std::string{});
                new_context["__yield_mode__"] = "NEXT";
                new_context["__yield_node_path__"] = node->path;
            } else {
                try {
                    auto tokens = bridge.pull_loop(*stream, budget_checker, 10000);
                    std::string concatenated;
                    for (const auto& t : tokens) concatenated += t;
                    new_context["__yield__"] = concatenated;
                } catch (const BudgetExceededException& e) {
                    std::string concatenated;
                    for (const auto& t : e.consumed_tokens) concatenated += t;
                    new_context["__yield__"] = concatenated;
                    new_context["__yield_budget_exceeded__"] = true;
                    throw;
                }
                new_context["__yield_mode__"] = "CONTINUE";
            }
            break;
        }
    }

    return new_context;
}

} // namespace agenticdsl
