# fix-orchestrator-token-passthrough — Proposal

## Why

Wave 1 #1 commit `5dc4569` (fix-generator-request-model-default) 修复 8 处 model 遮蔽时, 仅涉及 LLM 调用侧的 `req.params.model.clear()`, 未涉及 orchestration 层的 stop_token 透传。

`src/modules/cognitive/simple_orchestrator.cpp:125`:
```cpp
auto result = llm_->generate(req, {});
```

第二个参数 `{}` 是 `std::stop_token{}` 的硬编码默认值。任何外部 cancel 信号（如 Steering 中断、Agent 关闭、skill_interpreter 父进程退出）都无法传播至 LLM provider 的 generate 调用 → orchestrator 永远阻塞在 generate 上 → 用户无法中断长 turn。

这是 Wave 4 fix-yield-node-token-passthrough 同源问题（commit `5dc4569` 文档中标注的"注: token={} 属 fix-yield-node-token-passthrough scope"，yield 端已修，orchestrator 端同样需要）。

## Scope

**In scope**:
- `SimpleCognitiveOrchestrator::process` 加 `std::stop_token token = {}` 形参
- `SimpleCognitiveOrchestrator::react_once` 加 `std::stop_token token = {}` 形参
- process → react_once 转发 token
- react_once → llm_->generate 转发 token (替换硬编码 `{}`)
- 1 个新测试验证 token 透传

**Out of scope**:
- CognitiveWorker 内部的 stop_token 接入（属 fix-orchestrator-caller-token 后续 fix-up）
- SkillInterpreter timeout/stop_token 整合（属 fix-skill-interpreter-token-and-timeout 后续 fix-up）
- PlanExecuteLoop 内部 token 改造（Wave 2 已 ship plan_phase/verify_phase clear() + token）

## Impact

**API 影响**:
- BREAKING potential: process/react_once 多 1 形参，但默认 `= {}` 保证 100% 向后兼容（所有现有调用方零行为变更）
- CognitiveWorker 调用 process 时显式传 `std::stop_token{}` (零成本)，未来可注入真实 cancel token

**测试影响**:
- 0 个测试需修改（默认参数保留）
- 1 个测试新增 (Test 7 stop_token 透传)

**风险**:
- 低：默认 `{}` 与原硬编码 `{}` 完全等价；现有 7 个 tests + 1 个 example 验证零回归

## 依赖

无前置，与 Wave 4 fix-yield-node-token-passthrough (commit 9795784 + 60f3378 + c2806af) 并行可 ship。

## 升级触发

不适用。

## 估时

~30 分钟（含 Oracle ship-gate）。
