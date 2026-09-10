// src/core/types/tool_result.h
// 文件头注释
// 功能描述：工具执行结果的标准信封（envelope）数据结构。
//          Phase 0 X 阶段产物：定义 ToolResult 的 MVP 形态，
//          作为 Cognitive（B）与 InteractionBus（A）的共同契约前置。
//          Phase 1 Sprint 1a (S1a.T1) 扩展 P2-P4 字段 (ADR-0023):
//            - ErrorCode enum (替代 P1 自由 string, 11 个值)
//            - error_code / latency_ms / trace_id / metadata 4 个 optional 字段
// 设计依据：ADR-0023（ToolResult 标准化）+ plan §3 X 阶段
//          + openspec/changes/phase1-toolresult-standardization/specs/toolresult-p2-p4.md
//          (REQ-TR-001/002/003/004)
// 作者：AgenticDSL Phase 0 / Track X + Phase 1 Sprint 1a
// 最后修改日期：2026-06-16
#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace agenticdsl {

// === Phase 1 Sprint 1a 新增: ErrorCode enum (REQ-TR-001) ===
// 替代 Phase 0 X 阶段自由 string (e.g. "ERR_LLM.NETWORK")
// 17 个值: Unknown + P1 4 个 (ADR-0023 §3.1) + P2 6 个 (RETRY/SKIP/ABORT/AUDIT/TIMEOUT/RESOURCE_EXHAUSTED) + P3 6 个 (SkillInterpreter, 2026-07-21 skill-interpreter-real-loading change)
enum class ErrorCode {
  Unknown = 0,

  // P1 已有 (ADR-0023 §3.1, 4 个)
  PermissionDenied,    // 权限不足, 工具无权访问
  PathViolation,       // 路径越界 (e.g. shell guard 拒绝)
  DangerousCommand,     // 危险命令 (e.g. rm -rf /)
  ToolNotRegistered,   // 工具未注册

  // P2 新增 (6 个, Sprint 1a)
  Retry,               // 建议重试 (NetworkError 透传)
  Skip,                // 建议跳过此节点
  Abort,               // 终止整个流程
  Audit,               // 需要审计
  Timeout,             // 工具执行超时
  ResourceExhausted,   // 资源耗尽 (内存/磁盘/句柄)

  // P3 新增 (6 个, skill-interpreter-real-loading change 2026-07-21)
  // SkillInterpreter 专用错误码, 复用于 SkillResult.error_code
  SandboxViolation,    // 子进程触发 seccomp 白名单违规 (WIFSIGNALED + WTERMSIG == SIGSYS)
  MaxStepsExceeded,    // 父进程 IPC 循环 max_steps 强制触发, 已 SIGKILL 子进程
  Crash,               // 子进程因信号 (SIGSEGV/SIGABRT 等) 异常退出
  BudgetExhausted,     // Skill 内部 USD 预算耗尽 (SkillInterpreter::consume_budget)
  UnsupportedPlatform, // 在非 Linux 平台调用 SkillInterpreter::run() (编译时 #ifdef __linux__ 守卫)
  InvalidArg,          // 错误输入参数 (SKILL.md 不存在/frontmatter 缺失字段/EXIT_CHILD_PARSE_ERROR=64)

  // ADR-0073 D3 新增 (from-roadmap-phase-6c-schema-complete, 2026-08-18)
  // ToolCoordinator 4 步 sanitization pipeline 拒绝路径统一错误码
  // (schema_validate / coercion / required_field / business_rules 全部映射到本值)
  // 对应 JSON-RPC -32602 (Invalid params)
  InvalidParams,       // 工具入参校验失败 (schema 类型不匹配 / 必填字段缺失 / 危险模式)

  // P4 新增 (fix-cancel-errorcode-semantics, 2026-09-10)
  // LLM 消费侧 stop_token 触发的取消. 与 Abort (终止整个流程) 区分.
  // llm_error_to_error_code 将 LLMError::Code::Cancelled 映射到本值.
  Cancelled,           // 操作被外部 stop_token 取消
};

// 工具执行结果的标准信封 (MVP + P2-P4 扩展)
//
// 设计要点：
// - ok 字段是唯一的状态判定依据 (bool flag), data/meta 不参与判定。
// - data 用于承载工具的实际输出 (结构由工具自身约定)。
// - meta 用于承载 P1 MVP 元数据 (error_code/error_message/trace_id)。
// - error_code / latency_ms / trace_id / metadata (P2-P4) 是 optional,
//   默认 nullopt, 不影响 MVP 兼容性。
//
// 注意: 本阶段不引入 schema 版本号, 不修改 ok/data/meta 语义。
struct ToolResult {
  // === P0 X 阶段 MVP (3 字段, 不可变契约) ===
  bool ok = false;
  nlohmann::json data = nlohmann::json::object();
  nlohmann::json meta = nlohmann::json::object();

  // === Phase 1 Sprint 1a 新增 (P2-P4, 4 个 optional) ===
  // REQ-TR-001: error_code (P2) — 替代 P1 自由 string
  std::optional<ErrorCode> error_code;

  // REQ-TR-002: latency_ms (P3) — 工具执行耗时 (uint64_t milliseconds)
  std::optional<std::uint64_t> latency_ms;

  // REQ-TR-003: trace_id (P3) — 跨会话追踪 ID, 透传到 IInteractionBus
  std::optional<std::string> trace_id;

  // REQ-TR-004: metadata (P3) — 与 meta 共存, 扩展元数据
  std::optional<nlohmann::json> metadata;

  /**
   * @brief 构造成功结果
   * @param d 工具输出数据（移动语义）
   * @param m 可选元数据（移动语义）
   * @return ok=true 的 ToolResult
   */
  static ToolResult success(nlohmann::json d, nlohmann::json m = nlohmann::json::object());

  /**
   * @brief 构造失败结果 (P2 ErrorCode enum, Sprint 1a 推荐)
   * @param code 错误码枚举
   * @param msg 错误消息
   * @param m 可选额外元数据（移动语义）
   * @return ok=false 的 ToolResult, error_code 字段 + meta 双写
   */
  static ToolResult error(ErrorCode code,
                          std::string msg,
                          nlohmann::json m = nlohmann::json::object());

  /**
   * @brief 序列化为 JSON 对象
   * @return 包含 ok/data/meta + 4 个 optional 字段的 JSON
   */
  nlohmann::json to_json() const;

  /**
   * @brief 从 JSON 反序列化 (缺失字段使用默认值, optional 字段为 nullopt)
   * @param j 输入 JSON
   * @return 重建的 ToolResult
   */
  static ToolResult from_json(const nlohmann::json& j);
};

} // namespace agenticdsl
