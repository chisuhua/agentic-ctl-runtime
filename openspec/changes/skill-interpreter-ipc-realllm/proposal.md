## Why

`real-llm-core-coverage` Phase E 实施需求（tasks.md §Phase E），为 `SkillInterpreter`
IPC host function `dispatch_llm_generate` 建立真实 LLM 测试覆盖。

`SkillInterpreter` 是 PDK Agent 隔离执行层（ADR-0055）。子进程通过 IPC 调用父进程
LLM provider（`llm_generate` host function）。`dispatch_llm_generate` 构造
`GenerationRequest` 不设 `req.params.model` —— Wave 1 #1 Oracle P1-2 已 ship
`req.params.model.clear()`（5 个潜伏面之一，`skill_interpreter.cpp:664` 修复）。
但**未在真实 LLM 下验证**：
- IPC handler dispatch 真实 deepseek → return text → child pipe 接收 → 子进程 resume
- 真实 LLM 返回路径不撞墙（model 遮蔽已 ship 修复）
- 100-token prompt → 计费生效（Phase D 已 ship CostTrackingDecorator）

**用户原始关注**："交了 LLM 后延时会变长，需要测试来验证能工作"。
SkillInterpreter IPC dispatch 路径是真实 LLM 在隔离层的关键跳板。

## What Changes

### Scope (务实范围, 1h tractable)

**E.1 (Phase E 唯一本 change 实施)**: 测试 IPC host function `dispatch_llm_generate`
直接调用（非 posix_spawn 子进程）。构造 SkillCapability + SkillCapabilityPayload +
真实 LLM provider，dispatch 返回 IPCResponse。验证：
- 真实 deepseek 不撞墙（model 遮蔽已修复）
- response 含 LLM 真实返回文本（"hello" / "OK" 等）
- IPCResponse{ok=true, data.content=<真实文本>}

**E.5 (GAP 记录, 非本 change 实施)**:
- `skill_interpreter.cpp:659` `dispatch_llm_generate` 无 `std::stop_token` 超时保护
- 真实 LLM hang 时子进程永久 block
- 归属: Wave 4 `fix-skill-interpreter-token-and-timeout` 独立 change

### 关键技术约束

**A. 复用现有 MockBus + MockToolRegistry 测试基类** (既有 test_skill_interpreter.cpp 已有)

**B. host function 直接调用, 无 subprocess**:
- SkillInterpreterImpl::dispatch_llm_generate 是 private member
- 测试需 friend 声明 OR 公共 wrapper
- Phase E 设计: 在 SkillInterpreter 公开 `call_llm_generate_for_test()` wrapper
  (test-only, 标 [[deprecated("test only")]])

**C. helper 复用** (同 Wave 2/Phase D 模式):
- `agenticdsl::test::require_real_llm_env()` + `real_llm_env_skipped()` short-circuit
- `agenticdsl::test::real_llm_provider()` 构造 provider
- `req.params.model.clear()` Wave 1 #1 防御 (虽然 host function 内已 ship)

### Scope Boundaries (In)

- ✅ `tests/test_skill_interpreter.cpp` 扩展 1 case (E.1)
- ✅ SkillInterpreter 公开 `call_llm_generate_for_test()` wrapper
- ✅ 每个 case 含 skip short-circuit
- ✅ 既有 mock cases 零回归
- ✅ E.5 GAP 文档化（`docs/audits/skill-interpreter-llm-timeout-gap.md` 或 tasks.md §E.5）

### Scope Boundaries (Out)

- ❌ 不实施 full SkillInterpreter.execute posix_spawn 端到端（E.2-E.4, 4h+ tractability）
- ❌ 不创建 SKILL.md fixture（E.2, 需 fixture 文件 + posix_spawn 测试 harness）
- ❌ 不测 50KB pipe buffer（E.4, 需大 prompt + 跨 64KB buffer 验证）
- ❌ 不修 `dispatch_llm_generate` 无超时 GAP（E.5, 属 Wave 4 fix-skill-interpreter-token-and-timeout）
- ❌ 不实现 IPC 协议帧化优化（V2 deferred）

## Impact

**总计 ctest 影响**: +1 cases, baseline 231 → 232 (无 regression)
**CI 影响**: 全部 `[realllm]` tag, skip 模式 SUCCEED
**风险**: host function 直接调用不等同于 full IPC 子进程调用（pipe read/write + posix_spawn
未验证），但路径 95% 重叠（host function 是 IPC handler 核心逻辑）

## 升级触发 (Escalation)

若 Phase E 实施时发现:
- **host function 直接测试 OK, 但 IPC 子进程路径 FAIL** → 开
  `skill-interpreter-ipc-e2e-realllm` 独立 change（E.2-E.4 全量实施, 估时 4h+）
- **dispatch_llm_generate timeout 触发** → Wave 4 `fix-skill-interpreter-token-and-timeout` 立即升 P0

## 验证标准

- `cmake --build build -j$(nproc)` 编译通过 (0 error, 0 warning)
- `tests/test_skill_interpreter.cpp` 1 new case:
  - E.1: dispatch_llm_generate 真实 LLM → IPCResponse{ok=true, content=<真实文本>} — PASS
- skip 模式: 1 case SUCCEED short-circuit
- 既有 N mock cases 零回归
- 全量 `ctest -j$(nproc)` baseline 231 → 232 +1 PASS, 0 regression
- `openspec validate skill-interpreter-ipc-realllm --strict` exit 0
- `tools/adr_lint.py` 0 errors
- `tools/docs_drift_audit.py` 0 CRITICAL drift

## 估时

| 阶段 | 内容 | 估时 |
|---|---|---|
| 1 | 读 skill_interpreter.cpp:659 + 既有 test + 写 1 case + skip | 1 h |
| 2 | 真实 deepseek 验证 (本地有 key) | 30 min |
| 3 | commit + openspec validate + archive | 15 min |
| **Total** | | **~2 h** |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| host function 直接调用 ≠ full IPC 子进程调用 | 测试覆盖路径 95% (host function 是 IPC handler 核心), 剩余 5% pipe/posix_spawn 留 E.2-E.4 follow-up |
| SkillCapability allow_llm=false 阻止 dispatch | 测试显式设 allow_llm=true (mirroring 实际 IPC 路径) |
| model 遮蔽未修前实施 → 真实 LLM 必撞墙 | Wave 1 #1 已 ship 5 站点修复, 含 skill_interpreter.cpp:664 |
| dispatch_llm_generate 无超时 (E.5 GAP) | 本 change GAP 文档化, Wave 4 fix-skill-interpreter-token-and-timeout 跟踪 |
| MockToolRegistry 与真实 LLM 测试交互干扰 | 真实 LLM 测试不依赖 MockToolRegistry (dispatch_llm_generate 不调工具) |