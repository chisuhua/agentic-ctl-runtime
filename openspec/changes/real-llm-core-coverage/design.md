# Design — real-llm-core-coverage

## 测试架构 (Layer-based)

### Layer 1 — Cognitive/Domain 协调 (Phase A/B)
- **CognitiveWorker ReAct JSON 契约** — 真实 deepseek 响应格式验证
- **DomainWorkerPool 并发共享 provider** — N=4 worker 线程安全

### Layer 2 — PDK Agent Loops (Phase C)
- **PlanExecuteLoop verify "yes"** — 真实 LLM verify 响应

### Layer 3 — 装饰器 + IPC (Phase D/E)
- **CostTrackingDecorator 真实 token** — token 计费精度
- **SkillInterpreter IPC llm_generate** — seccomp + pipe 真实 LLM

### Layer 4 — 流式 + 摘要 (Phase F/G, P1)
- **YieldNode 流式取消** — 需先补 token 透传 (deferred)
- **ContextCompactor 真实 LLM** — 摘要稳定性

## helper 迁移设计

`chat-real-llm-coverage` 的 `examples/pdk_chat_demo/tests/test_helpers/real_llm_env.h` 应提升到项目级:

```
tests/test_helpers/real_llm_env.h          [NEW — 同步 from pdk_chat_demo]
```

**复用设计**:
- API 完全一致 (`require_real_llm_env` / `real_llm_config` / `real_llm_provider`)
- env var 真值表一致 (SKIP=1 → skip / key → run / 无 key → FAIL)
- 错误码一致 (`AuthenticationError` / `NetworkError`)

**差异**:
- include 路径调整 (从 `examples/pdk_chat_demo/tests/test_helpers/` → `tests/test_helpers/`)
- pdk_chat_demo helper 在 archive 时保留作为内联副本 (便于参考)

## 各 Phase 关键测试模式

### Phase A — CognitiveWorker ReAct

```cpp
TEST_CASE("CognitiveWorker ReAct JSON contract with real LLM") {
  pdk_chat_demo::testing::require_real_llm_env();  // 复用 helper
  auto cfg = pdk_chat_demo::testing::real_llm_config();
  // ... 构造 CognitiveWorker + handler 工具 ...
  auto result = worker.submit(task);
  // JSON 验证 (宽松: 子串 + 容错)
  REQUIRE(result.response.find("\"tool\":") != std::string::npos);
}
```

### Phase B — DomainWorkerPool 并发

```cpp
TEST_CASE("DomainWorkerPool N=4 workers concurrent generate") {
  // 4 个 worker 各自 submit task,共享 1 个 provider 实例
  // 验证: 4 个 result 都返回,无 race,无 429 重试风暴
}
```

### Phase C — PlanExecuteLoop verify

```cpp
TEST_CASE("PlanExecuteLoop verify 'yes' with real LLM") {
  // plan_phase 真实 LLM → 合法 DSL
  // execute_phase parse + 执行
  // verify_phase 真实 LLM → 含 "yes" (大小写不敏感)
}
```

### Phase D — CostTrackingDecorator

```cpp
TEST_CASE("CostTrackingDecorator accurate token charge") {
  auto decorated = std::make_unique<CostTrackingDecorator>(real_provider);
  auto result = decorated->generate(req);
  REQUIRE(result.value().completion_tokens > 0);
  REQUIRE(budget.remaining() < budget.original);
}
```

### Phase E — SkillInterpreter IPC

```cpp
TEST_CASE("SkillInterpreter llm_generate via IPC") {
  // SKILL.md 含 llm_generate("Say hello")
  // 子进程调真实 deepseek
  // 父进程 pipe 接收 result
  // 验证 result 与真实 LLM 响应一致
}
```

### Phase F — YieldNode 取消 GAP (deferred)

```cpp
TEST_CASE("YieldNode streaming cancellation GAP (known broken)") {
  // 记录已知问题: token = {}
  // 本 test 仅做 INFO() 标记,不实际取消
  WARN("YieldNode token passthrough not wired (fix-up change pending)");
}
```

### Phase G — ContextCompactor 摘要

```cpp
TEST_CASE("ContextCompactor summarization with real LLM") {
  // 50 message history
  // compact → 真实 LLM 摘要
  // 验证 history size 减少 + response 非空
}
```

## CI 集成设计

**当前 CI**: `.github/workflows/ci.yml` 运行 `cmake --preset tests` (默认 examples=OFF)
**问题**: 本 change 测试在 core 树,会被构建运行 → 无 key 时 FAIL → CI 红
**修复**: ci.yml 添加 env `HYDRAFORGE_SKIP_REAL_LLM: "1"` (前瞻,与 sibling change 同步提交)

## 取消覆盖断裂追踪

本 change 在测试中**显式记录** 6 个 LLM 调用点的 token 透传断裂 (非修复, 仅文档化):

```cpp
// record_token_passthrough_gaps() test:
// 输出: "simple_orchestrator:118 ❌ token={}"
//       "node_executor:576 (YieldNode) ❌ token={}"
//       "gepa_loop:116 ❌ token={}"
//       "skill_interpreter:659 ❌ token={}"
//       "context_compactor:64 ❌ token={}"
```

这为后续 fix-up change 提供验收清单。

## 实施顺序

1. **Phase 0**: helper 迁移 (项目级) — 其他 phase 依赖
2. **Phase A**: CognitiveWorker (P0, 关键路径)
3. **Phase B**: DomainWorkerPool (P0, 并发安全)
4. **Phase C**: PlanExecuteLoop verify (P0)
5. **Phase D**: CostTrackingDecorator (P0)
6. **Phase E**: SkillInterpreter IPC (P1)
7. **Phase F**: YieldNode GAP (deferred)
8. **Phase G**: ContextCompactor (P1)
9. **Phase H**: 验证 + archive

每 phase 完成后 `ctest -R <new tests>` 验证再启下一 phase。