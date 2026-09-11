// include/agenticdsl/skill/skill_interpreter.h
// 功能描述：SkillInterpreter PIMPL 公开头文件 — ADR-0055 定义的 SKILL.md
//          命令式 DSL 隔离执行接口。
//          提供 SkillInterpreter::run() 方法启动隔离进程执行 .skill.md 文件。
//          Sprint 29: 新增 ITimerService* 可选参数支持硬 deadline 注入测试。
// 设计依据：ADR-0055（Skill 隔离执行模型）
//          + openspec/changes/skill-interpreter-real-loading/design.md
//          + openspec/changes/skill-interpreter-real-loading/specs/
//          + openspec/changes/skill-interpreter-timer-migration/design.md (Sprint 29)
// 作者：AgenticDSL SkillInterpreter change
// 最后修改日期：2026-09-12 (Sprint 29 timer migration)
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agenticdsl/contract/iinteraction_bus.h"
#include "agenticdsl/contract/itool_registry.h"
#include "agenticdsl/types/layered_context.h"

namespace agenticdsl {

// 前向声明（来自 common/llm/llm_types.h）
class ILLMProvider;

// 前向声明（来自 contract/timer_service.h）— Sprint 29 注入式 deadline 测试
// 头文件不直接 include timer_service.h 以保持最小 include surface (ADR-0021 §3.5)
class ITimerService;

// === SkillCapability — 注入子进程的能力限制 ===
// 字段类型对齐 ADR-0055 §决策 3
// V1 硬编码默认值（default_skill_capability()），derive_capability V2 deferred
struct SkillCapability {
  std::vector<std::string> allowed_tools;   // 白名单工具名列表
  std::vector<std::string> allowed_topics;  // emit_event topic 白名单（空 = 全部允许）
  uint32_t max_steps = 50;                  // IPC 循环最大步数（父进程强制）
  std::chrono::milliseconds timeout_ms{30000};  // 子进程超时（默认 30s）
  double budget_limit_usd = 0.01;           // USD 预算上限
  bool allow_llm = false;                   // 是否允许 llm_generate
};

// === SkillResult — SkillInterpreter::run() 返回值 ===
struct SkillResult {
  bool success = false;
  ErrorCode error_code = ErrorCode::Unknown;
  nlohmann::json output = nlohmann::json::object();  // return 语句的值
  std::string stderr_content;                         // 子进程 stderr
  bool stderr_truncated = false;                      // stderr 超过 1MB 截断
  uint64_t duration_ms = 0;                           // 执行耗时
  int child_exit_status = -1;                         // 子进程退出码
};

// === SkillInterpreter — ADR-0055 隔离执行引擎 ===
// PIMPL 模式：公开头文件仅暴露最小接口，实现细节在 skill_interpreter.cpp
// V1 构造函数不接受 IBudgetController*（内部 std::atomic<double> 计数器，见 design.md Decision 11）
class SkillInterpreter {
 public:
  /// V1 构造函数（最终确定，不接受 IBudgetController*）
  /// @param tools 工具注册表引用
  /// @param bus   事件总线引用
  /// @param llm   LLM provider 指针（可为 nullptr）
  /// @param ctx   分层上下文指针（可为 nullptr）
  /// @param timer ITimerService 注入指针（Sprint 29 新增，默认 nullptr）.
  ///              nullptr 时内部自动 make_default_timer_service() (eager, D9).
  ///              非 nullptr 时使用注入的 timer (测试可传 FakeTimer).
  ///              生命周期契约 (D10, 镜像 workflow_callback_channel.h:47-51):
  ///              - 注入 timer 时,调用方必须保证: SkillInterpreter 析构前,timer
  ///                的 callback 已无 in-flight 执行 (cancel 不等待已收集但未执行的
  ///                callback, [this] 捕获可能触发 UAF).
  ///              - 推荐顺序: 析构 SkillInterpreter → 析构 timer.
  ///              - nullptr 路径安全 (unique_ptr RAII 自动 join).
  SkillInterpreter(IToolRegistry& tools,
                   IInteractionBus& bus,
                   ILLMProvider* llm,
                   const LayeredContext* ctx,
                   ITimerService* timer = nullptr);

  /// 析构函数：自动 waitpid() 防止僵尸进程
  ~SkillInterpreter();

  /// 禁用拷贝
  SkillInterpreter(const SkillInterpreter&) = delete;
  SkillInterpreter& operator=(const SkillInterpreter&) = delete;

  /// 允许移动
  SkillInterpreter(SkillInterpreter&&) noexcept;
  SkillInterpreter& operator=(SkillInterpreter&&) noexcept;

  /// 执行 SKILL.md 文件
  /// @param skill_path .skill.md 文件路径
  /// @param cap        capability 限制
  /// @param token      取消信号 (默认空 token 保持向后兼容). 取消时立即 SIGKILL 子进程
  ///                   并返回 ErrorCode::Abort, 不等 cap.timeout_ms.
  /// @return SkillResult 包含执行结果、stderr、退出码等
  SkillResult run(const std::string& skill_path,
                  const SkillCapability& cap,
                  std::stop_token token = {});

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

/// V1 硬编码默认 capability（pdk_chat_demo 场景）
inline SkillCapability default_skill_capability() {
  SkillCapability cap;
  cap.allowed_tools = {"code_review/run", "fs.read"};
  cap.allowed_topics = {};
  cap.max_steps = 50;
  cap.timeout_ms = std::chrono::milliseconds(30000);
  cap.budget_limit_usd = 0.01;
  cap.allow_llm = false;
  return cap;
}

/// 子进程入口函数（在独立 translation unit skill_child_main.cpp 中实现）
/// 仅被 --skill-child 分支调用
int skill_child_main(int argc, char** argv);

}  // namespace agenticdsl