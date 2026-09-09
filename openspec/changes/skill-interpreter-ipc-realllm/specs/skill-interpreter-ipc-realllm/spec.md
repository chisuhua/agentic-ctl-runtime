# Spec: skill-interpreter-ipc-realllm

## Purpose

为 `SkillInterpreterImpl::dispatch_llm_generate` IPC host function 建立真实 LLM 测试覆盖（Phase E 务实范围：1 case, host function direct test, 无 posix_spawn 子进程）。

记录 E.5 GAP（`skill_interpreter.cpp:659` 无超时保护）归属 Wave 4 fix-skill-interpreter-token-and-timeout。

E.2-E.4 full execute 端到端（SKILL.md fixture + 50KB pipe buffer）延后到独立 change `skill-interpreter-ipc-e2e-realllm`。

## ADDED Requirements

### Requirement: dispatch_llm_generate 真实 LLM SHALL succeed via host function test

`SkillInterpreter::call_llm_generate_for_test(req, cap)` (test-only wrapper 转发 `impl_->dispatch_llm_generate`) SHALL 在真实 deepseek LLM 下返回 `IPCResponse{ok=true, data.content=<非空真实文本>}`, 验证 IPC dispatch 路径核心逻辑在真实 LLM 下工作（model 遮蔽已 ship 修复, 实际 LLM call 成功返回）。

#### Scenario: SkillInterpreter IPC dispatch_llm_generate real LLM host
- GIVEN real deepseek configured
- AND SkillCapability{allow_llm=true, budget_limit_usd=1.0}
- AND IPCRequest{call="llm_generate", params={prompt="Say OK in one word", model="deepseek-v4-flash"}}
- WHEN `skill.call_llm_generate_for_test(req, cap)` direct invoke (无 subprocess)
- THEN `response.ok == true`
- AND `response.data.contains("content")` 且 `response.data["content"]` 非空
- AND `response.data["llm_error"]` 不存在 (无错误)

### Requirement: scope 边界 (Out of Scope)

本 change SHALL NOT 修改 SkillInterpreter production IPC 路径, 不实施 posix_spawn 子进程端到端测试, 不修 E.5 timeout GAP; 下列 SHALL 明确排除.

#### Scenario: SkillInterpreterImpl::dispatch_llm_generate 生产代码 SHALL NOT be modified
- 理由: IPC host function 核心逻辑稳定契约 (ADR-0055 Sprint 22 ship), 不修改
- AND model 遮蔽已 Wave 1 #1 ship 修复 (skill_interpreter.cpp:664)
- AND call_llm_generate_for_test wrapper 仅 PIMPL 转发, 零生产路径侵入

#### Scenario: Full SkillInterpreter.execute posix_spawn end-to-end SHALL NOT be in this change
- 归属: 独立 change `skill-interpreter-ipc-e2e-realllm` (估时 4h+)
- 理由: 本 change 务实范围 1h tractable, full subprocess 测试需 SKILL.md fixture + posix_spawn 测试 harness

#### Scenario: 50KB large response pipe buffer test SHALL NOT be in this change
- 归属: `skill-interpreter-ipc-e2e-realllm` E.4 (50KB pipe buffer)
- 理由: 需大 prompt 触发大响应 + 跨 64KB buffer 帧化验证, 4h+ 估时

#### Scenario: dispatch_llm_generate timeout GAP SHALL NOT be fixed in this change
- **GAP 位置**: `src/modules/skill_interpreter/skill_interpreter.cpp:659`
  `SkillInterpreterImpl::dispatch_llm_generate`
- **GAP 现象**: 函数签名无 `std::stop_token` 参数, 真实 LLM hang 时子进程永久 block
- **影响**: child process IPC 调用 dispatch_llm_generate 时若 deepseek hang, child 永久 block 直至父进程 kill
- **归属**: Wave 4 `fix-skill-interpreter-token-and-timeout` 独立 change (估时 0.5-1 day)
- **本 change**: GAP 文档化 (tasks.md §E.5 末)

## 验证标准

- `cmake --build build -j$(nproc)` 编译通过 (0 error, 0 warning)
- `tests/test_skill_interpreter.cpp` 1 new case:
  - E.1: dispatch_llm_generate 真实 LLM → IPCResponse{ok=true, content=<真实文本>} — PASS
- skip 模式: 1 case SUCCEED short-circuit (CI 友好)
- 既有 N mock cases 零回归
- 全量 `ctest -j$(nproc)` baseline 231 → 232 +1 PASS, 0 regression
- `openspec validate skill-interpreter-ipc-realllm --strict` exit 0
- `tools/adr_lint.py` 0 errors
- `tools/docs_drift_audit.py` 0 CRITICAL drift

## 依赖

| 上游 | 状态 | 影响 |
|---|---|---|
| `real-llm-core-coverage` Phase 0+A | ✅ SHIPPED | helper 自测已可用 |
| Wave 1 #1 fix-generation-request-model-default | ✅ SHIPPED | req.params.model.clear() 已 ship 5 站点, 含 skill_interpreter.cpp:664 |
| Wave 1 #2 fix-cloud-adapter-multithreading | ✅ SHIPPED | dispatch 走 SerializingDecorator 包装 cloud adapter |
| Wave 3 Phase D cost-tracking-decorator-realllm | ✅ SHIPPED | 100-token prompt 计费验证就绪 (Phase E 复用) |

| 下游 (follow-up) | 内容 |
|---|---|
| `skill-interpreter-ipc-e2e-realllm` (独立) | E.2-E.4 full execute + 50KB pipe buffer (估时 4h+) |
| `fix-skill-interpreter-token-and-timeout` (Wave 4) | dispatch_llm_generate 加 stop_token + 子进程优雅退出 |

## References

- **设计依据**: `src/modules/skill_interpreter/skill_interpreter.cpp:659`
  (dispatch_llm_generate host function) + ADR-0055 (SkillInterpreter 架构)
- **既有测试**: `tests/test_skill_interpreter.cpp` (N mock cases, MockBus +
  MockToolRegistry 基类)
- **关联修复**: Wave 1 #1 `fix-generation-request-model-default`
  (skill_interpreter.cpp:664 req.params.model.clear())
- **测试模式**: Wave 2 `plan-execute-loop-realllm` + Wave 3 Phase D
  `cost-tracking-decorator-realllm` (require_real_llm_env + skip short-circuit)
- **追溯 ADR**: ADR-0055 (SkillInterpreter 架构, Sprint 22 ship)