# fix-orchestrator-token-passthrough

**Status**: Draft (scaffold)

## Scope

Wave 4 fix-up change. `SimpleCognitiveOrchestrator::react_once` (`src/modules/cognitive/simple_orchestrator.cpp:125`) 硬编码 `llm_->generate(req, {})` — 与 Wave 1 #1 GAP 同源, 但 Wave 1 #1 未涉及 orchestrator.

1 改动 (3 文件):
- `include/agenticdsl/cognitive/simple_orchestrator.h:67` — `process(session_id, cb)` → `process(session_id, cb, std::stop_token = {})`
- `include/agenticdsl/cognitive/simple_orchestrator.h:79` — `react_once(prompt)` → `react_once(prompt, std::stop_token = {})`
- `src/modules/cognitive/simple_orchestrator.cpp:91` — process 转发 token 至 react_once
- `src/modules/cognitive/simple_orchestrator.cpp:125` — react_once 转发 token 至 llm_->generate (替换 `{}`)

1 测试: `tests/test_simple_orchestrator.cpp` 新增 Test 7
- pre-cancel stop_token → orch.process(s7, cb, ss.get_token())
- RecordingLLMProvider 新增 `last_token_stop_requested` 字段 (additive, 不影响其他 tests)
- 断言 `raw->last_token_stop_requested == true`

## Why

与 Wave 4 fix-yield-node-token-passthrough 同源, 详见根因 + 模式 #1 沉淀 (AGENTS.md 决策层).

## 依赖

无前置. 与 fix-yield-node-token-passthrough 并行可 ship.

## 升级触发

不适用 (此 change 是 Wave 4 后续 fix-up, 非升级触发型).

## 估时

~30 分钟 (已 commit 4598fda, scaffold 待 commit, Oracle ship-gate 待发起).
