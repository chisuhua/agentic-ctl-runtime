# Spec: fix-generation-request-model-default

## Purpose

系统性修复 `LLMParams` (=`LLMConfig` 别名) 默认 `model="gpt-4o-mini"` 非空遮蔽
CloudLLMAdapter 配置真实 model 的 bug 在 5 个潜伏站点。本 change 覆盖：

- `src/modules/executor/node_executor.cpp:356` (GenerateSubgraphNode ll_call)
- `src/modules/executor/node_executor.cpp:574` (YieldNode 流式)
- `src/modules/skill_interpreter/skill_interpreter.cpp:657-659` (IPC llm_generate)
- `src/core/context_compactor.cpp:60` (compact 摘要)
- `src/modules/cognitive/gepa_loop.cpp:115` (GEPA 反射)

+ `simple_orchestrator.cpp:118` (由 `real-llm-core-coverage` Phase A 已 ship).

## ADDED Requirements

### Requirement: GenerationRequest at caller sites SHALL pass empty model to provider

每个站点构造 `GenerationRequest req/request/gen_req` 后, 调用 `generate()` 或 `generate_stream()` 之前 SHALL 确保 `req.params.model` 为空 (通过 `req.params.model.clear()` 或保证构造时 `params.model` 默认空). 否则 `CloudLLMAdapter::build_request_body` L164 (`req.params.model.empty() ? config_.model : req.params.model`) 会拿非空默认值遮蔽真实 model → server 拒绝.

#### Scenario: node_executor.cpp:356 GenerateSubgraphNode passes empty model
- GIVEN GenerateSubgraphNode handler
- WHEN 构造 `GenerationRequest req` 调 llm_->generate
- THEN RecordingLLMProvider 收到的 `req.params.model` 为空
- AND cloud adapter fallback 到 `config_.model` (如 deepseek-v4-flash)

#### Scenario: node_executor.cpp:574 YieldNode passes empty model
- GIVEN YieldNode handler
- WHEN 构造 `GenerationRequest req` 调 llm_->generate_stream
- THEN Recorder 收到的 `req.params.model` 为空
- AND token={} GAP 已知 (token passthrough 属 `fix-yield-node-token-passthrough`)

#### Scenario: skill_interpreter.cpp:657-659 IPC llm_generate passes empty model
- GIVEN SkillInterpreter.execute 解析 `llm_generate("...")` 语句
- WHEN skill_child_main 调 llm_->generate
- THEN recorder 收到的 `req.params.model` 为空

#### Scenario: context_compactor.cpp:60 compact passes empty model
- GIVEN ContextCompactor.compact(ctx)
- WHEN 构造摘要 GenerationRequest 调 generate
- THEN recorder 收到的 `req.params.model` 为空

#### Scenario: gepa_loop.cpp:115 reflection passes empty model
- GIVEN GEPA loop 构造 `GenerationRequest request("Reflect on failed execution ...")`
- WHEN 调 llm_->generate
- THEN recorder 收到的 `request.params.model` 为空

#### Scenario: Static contract check prevents regression (scripts/check-model-default-cleared.sh)
- GIVEN 5 站点 (含 simple_orchestrator) 修复完成
- WHEN `scripts/check-model-default-cleared.sh` 运行
- THEN grep 在 5 站点附近找到 `req.params.model.clear()` 或 `request.params.model.clear()`
- AND 退出码 0
- AND 漏站点时退出码 1 + stderr 报告具体漏站点

### Requirement: Telemetry model="" side effect SHALL be documented

修复后 decorator 链记录/发射 `model=""` 副作用 SHALL 被 spec/tasks.md 明确文档化, Phase D (Cost) assertion SHALL 容忍空 model 或显式设值 (与 Production Cost 计数行为解耦).

#### Scenario: Decorator chain records model="" after fix
- GIVEN 5 站点修复后
- WHEN CostTrackingDecorator.record_llm_call(t, model) 触发
- THEN model="" (此前错误但非空 "gpt-4o-mini")
- AND 计费仍按 token 数, **不影响 budget 扣费金额**
- AND Phase D (Cost) 真实 LLM assertion 容忍 model="" 或显式设值 (提醒已加
  `real-llm-core-coverage/tasks.md` §D 阶段)

#### Scenario: TracingDecorator emits model="" in llm.request/response events
- GIVEN bus_ 已注入, CostTrackingDecorator 链启用
- WHEN llm.request 事件发射
- THEN payload 含 `model=""` (而非 "gpt-4o-mini")
- AND Phase D 事件 assertion 容忍空 model

### Requirement: scope 边界 (Out of Scope)

本 change SHALL NOT 修改 production code 中与 model 遮蔽无关的部分. 以下 SHALL 明确排除在 scope 外, 各自归属独立 change.

#### Scenario: LLMConfig default model SHALL NOT be changed
- GIVEN `LLMConfig::model` 默认 `"gpt-4o-mini"`
- AND 该默认值承担"未设 model 的请求仍可发 OpenAI 兼容端点"的隐性契约
- WHEN 修改默认值会破坏所有依赖默认值的路径 (爆炸半径远超本 change)
- AND 改空会让发空 model → server 拒绝
- THEN 本 change 不修改默认值
- AND `LLMParams = LLMConfig` 别名混淆 per-request params 与 provider config 是
  独立大重构 (out of scope, 备注给后续 ADR)

#### Scenario: CloudLLMAdapter model selection logic SHALL NOT be changed
- GIVEN `CloudLLMAdapter::build_request_body` L164 行为正确
  (`req.params.model.empty() ? config_.model : req.params.model`)
- AND 问题仅在调用方传非空默认值
- THEN 本 change 不修改 adapter, 仅修复调用方

#### Scenario: Token passthrough fixes SHALL NOT be in this change
- 归属: `fix-yield-node-token-passthrough` / `fix-orchestrator-token-passthrough` /
  `fix-skill-interpreter-token-and-timeout`
- 理由: 涉及生产代码 `std::stop_token` 透传, 与 model 修复独立

### Requirement: 升级触发 (Escalation trigger)

若 `real-llm-core-coverage` Phase C-G 实施时发现 **≥3 个** 需同类 model 修复的站点 (本 change 5 站点之外), SHALL 立即纳入本 change 并升为 Phase B-G 前置 P0 (不再 deferred). 升级条件 SHALL 量化定义避免主观判断争论.

#### Scenario: ≥3 site fix shall escalate to Phase C P0
- GIVEN `real-llm-core-coverage` Phase C-G 实施时发现 **≥3 个** 需同类 model 修复的站点
- AND 本 change 已覆盖的 5 个站点是已实测的潜伏面
- WHEN Phase C 暴露新站点 (本 change 5 个之外的)
- THEN 该站点应立即纳入本 change (追加 commit)
- AND `real-llm-core-coverage/tasks.md` §D 阶段前应确保本 change 已 ship

## 验证标准

- 5 站点 (含 orchestrator) 修复完成, 6 个 recording provider 单测 (含 orchestrator)
- `scripts/check-model-default-cleared.sh` 退出码 0
- `ctest -j$(nproc)` baseline 224 → 229+5 PASS, 0 regression
- 无 API key 时新测试 PASS (CI 友好)
- `openspec validate fix-generation-request-model-default --strict` exit 0
- `tools/adr_lint.py` 0 errors

## References

- **根因**: `real-llm-core-coverage` Phase A ship (commit `afc2d1b`) 实证
- **系统性记录**: Oracle 审查 session `ses_f7f5ef175ffeGKhxXLfBJjzLVX`
- **关联修复**: `real-llm-core-coverage` Phase A.5 (recording provider 守卫模式)
- **设计依据**: `src/common/llm/cloud_adapter.cpp:164` build_request_body fallback 逻辑
