// tests/test_command_help.cpp
// chat-real-llm-coverage Phase A.1: /help 命令路径覆盖
//
// 验证:
//   A.1.2 make_help_command_spec 字段正确
//   A.1.3 handler 调用真实 CommandRegistry.render_help()
//   A.1.4 handler 在 g_command_registry == nullptr 时返回 error
//   A.1.5 render_help 输出含 7 个注册命令 + /exit 保留字
//
// 依赖模式 (参照 test_pdk_chat_unknown_command.cpp):
//   ToolRegistry → register_provider_switch_stub_tool → AgentModePolicy
//   → ApprovalCallback → ToolCoordinator → CommandRegistry → register specs

#include <catch_amalgamated.hpp>

#include <common/tools/command_registry.h>
#include <common/tools/tool_coordinator.h>
#include <common/tools/registry.h>
#include <common/policy/agent_mode_policy.h>
#include <common/policy/approval_callbacks.h>
#include "commands/command_globals.h"
#include "commands/help_command.h"
#include "commands/compact_command.h"
#include "commands/model_command.h"
#include "commands/tree_command.h"
#include "commands/fork_command.h"
#include "commands/clone_command.h"
#include "commands/cancel_command.h"
#include "tools/provider_switch_stub.h"

using agenticdsl::AgentModePolicy;
using agenticdsl::ApprovalCallback;
using agenticdsl::CommandRegistry;
using agenticdsl::IExecutionPolicy;
using agenticdsl::make_test_auto_callback;
using agenticdsl::ToolCallContext;
using agenticdsl::ToolCoordinator;
using agenticdsl::ToolRegistry;

// RAII guard: 在每个 TEST_CASE 入口注入测试 CommandRegistry, 出口恢复原值
// 防止其他测试 binary 跨进程泄漏 (虽然 binary 隔离, 但同 binary 内多 case 仍需 save/restore)
struct CommandRegistryGuard {
  agenticdsl::CommandRegistry* saved;
  CommandRegistryGuard(agenticdsl::CommandRegistry* new_reg)
      : saved(pdk_chat_demo::g_command_registry) {
    pdk_chat_demo::g_command_registry = new_reg;
  }
  ~CommandRegistryGuard() { pdk_chat_demo::g_command_registry = saved; }
};

// Build a fully-populated CommandRegistry matching main.cpp:525-531
// 7 specs + ToolCoordinator wiring ( (per spec requirement A.1.5)
struct CommandRegistryFixture {
  ToolRegistry registry;
  std::shared_ptr<AgentModePolicy> policy;
  ApprovalCallback callback;
  ToolCoordinator coordinator;
  CommandRegistry cmd_reg;

  CommandRegistryFixture()
      : registry(),
        policy(std::make_shared<AgentModePolicy>()),
        callback(make_test_auto_callback(true)),
        coordinator(registry, policy, callback),
        cmd_reg(&coordinator) {
    pdk_chat_demo::register_provider_switch_stub_tool(registry);
    cmd_reg.register_command(pdk_chat_demo::make_help_command_spec());
    cmd_reg.register_command(pdk_chat_demo::make_compact_command_spec());
    cmd_reg.register_command(pdk_chat_demo::make_model_command_spec());
    cmd_reg.register_command(pdk_chat_demo::make_tree_command_spec());
    cmd_reg.register_command(pdk_chat_demo::make_fork_command_spec());
    cmd_reg.register_command(pdk_chat_demo::make_clone_command_spec());
    cmd_reg.register_command(pdk_chat_demo::make_cancel_command_spec());
  }
};

// ===== A.1.2 spec 字段 =====

TEST_CASE("/help spec fields are correct", "[chat-real-llm-coverage][command]") {
  auto spec = pdk_chat_demo::make_help_command_spec();
  REQUIRE(spec.name == "/help");
  REQUIRE_FALSE(spec.description.empty());
  REQUIRE(spec.plugin_origin == "pdk_chat_demo");
  REQUIRE(spec.handler != nullptr);
}

// ===== A.1.5 render_help 8 命令名 =====

TEST_CASE("/help render lists all 7 registered commands + /exit reserved",
          "[chat-real-llm-coverage][command]") {
  CommandRegistryFixture fix_obj;
  CommandRegistryGuard guard(&fix_obj.cmd_reg);

  agenticdsl::ToolCallContext ctx;
  std::string output = fix_obj.cmd_reg.render_help();

  // 7 注册命令
  for (const char* name : {"/help", "/compact", "/model", "/tree",
                            "/fork",  "/clone",  "/cancel"}) {
    INFO("missing command: " << name);
    REQUIRE(output.find(name) != std::string::npos);
  }
  // /exit 保留字
  INFO("missing reserved: /exit");
  REQUIRE(output.find("/exit") != std::string::npos);
}

// ===== A.1.3 handler 调用 render_help =====

TEST_CASE("/help handler calls render_help() with valid registry",
          "[chat-real-llm-coverage][command]") {
  CommandRegistryFixture fix_obj;
  CommandRegistryGuard guard(&fix_obj.cmd_reg);

  auto spec = pdk_chat_demo::make_help_command_spec();
  agenticdsl::ToolCallContext ctx;
  std::string output = spec.handler(ctx);

  // handler 直接调用 render_help(), 内容应一致
  REQUIRE(output == fix_obj.cmd_reg.render_help());
  // 至少含 /help (证明 registry 真正被调用, 不是 fallback)
  REQUIRE(output.find("/help") != std::string::npos);
}

// ===== A.1.4 handler 在 null registry 时返回 error =====

TEST_CASE("/help handler returns error when g_command_registry is null",
          "[chat-real-llm-coverage][command]") {
  // 显式置 null, 测试结束后 RAII 恢复
  CommandRegistryGuard guard(nullptr);

  auto spec = pdk_chat_demo::make_help_command_spec();
  agenticdsl::ToolCallContext ctx;
  std::string output = spec.handler(ctx);

  REQUIRE(output.find("CommandRegistry not injected") != std::string::npos);
}