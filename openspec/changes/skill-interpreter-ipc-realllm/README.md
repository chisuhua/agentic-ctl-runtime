# skill-interpreter-ipc-realllm

**Status**: Draft (scaffold)

## Scope

`real-llm-core-coverage` Phase E 实施: `SkillInterpreter` IPC host function
`dispatch_llm_generate` 真实 deepseek LLM 端到端测试。**务实范围**:
- E.1: host function direct test (无 posix_spawn 子进程, 1h tractable)
- E.5: 记录 GAP `skill_interpreter.cpp:659` 无超时 → Wave 4 `fix-skill-interpreter-token-and-timeout`
- E.2/E.3/E.4: full SkillInterpreter.execute posix_spawn 端到端 + 50KB pipe buffer — deferred
  (4h+ tractability, 需 SKILL.md fixture + 子进程 spawn + pipe IPC 集成)

## Why

SkillInterpreter 是 PDK Agent 隔离执行层（ADR-0055），子进程通过 IPC 调用父进程
LLM provider。`dispatch_llm_generate` host function（`skill_interpreter.cpp:659`）
构造 `GenerationRequest` 不设 `req.params.model` — Wave 1 #1 已 ship
`req.params.model.clear()`（Oracle P1-2 5 站点之一）。但**未在真实 deepseek 下验证**：
- IPC handler dispatch 真实 LLM → return text → child pipe 接收
- 真实 LLM 返回路径不撞墙 (model 遮蔽已修复)
- 100-token prompt → 计费生效 (Phase D 已 ship)

## 验证

- `openspec validate skill-interpreter-ipc-realllm --strict` exit 0
- `ctest -j$(nproc)` baseline 231 → 232 +1 new, 0 regression
- skip 模式 SUCCEED；真实 deepseek E.1 PASS（本地有 key）
- 既有 `test_skill_interpreter.cpp` 零回归

## 依赖

- **依赖**: `real-llm-core-coverage` Phase 0+A 已 ship（helper 可用）
- **依赖**: Wave 1 #1 fix-generation-request-model-default 已 ship（req.params.model.clear() 应用于 skill_interpreter.cpp:664, IPC dispatch 路径）
- **依赖**: Wave 1 #2 fix-cloud-adapter-multithreading 已 ship（dispatch 走 SerializingDecorator 包装 cloud adapter）

## follow-up (本 change archive 时记入)

- E.2/E.3/E.4: full SkillInterpreter.execute posix_spawn + SKILL.md fixture +
  50KB pipe buffer — 独立 change, 估时 4h+
- E.5 GAP: `skill_interpreter.cpp:659` 无 `std::stop_token` 超时保护, 真实 LLM hang 时
  子进程永久 block — `fix-skill-interpreter-token-and-timeout` 独立 change (Wave 4)

## Artifacts

- `proposal.md` — Why / Scope (务实范围) / Impact / 依赖 / 升级触发 / 估时
- `design.md` — 测试架构 + helper 复用 + host function API + E.1 模板 + E.5 GAP 记录
- `tasks.md` — 10 sub-tasks across 3 phases
- `specs/skill-interpreter-ipc-realllm/spec.md` — 2 ADDED Requirements (E.1 PASS + E.5 GAP)