## Why

`real-llm-core-coverage` Phase C 实施需求（tasks.md §Phase C），为非 pdk_chat_demo
路径的 10 个 LLM 调用点建立生产级真实 LLM 测试覆盖。`PlanExecuteLoop::plan_phase`
和 `verify_phase` 都构造 `GenerationRequest` 但**不显式设 `params.model`**，是
`LLMParams` 默认遮蔽的 5 个潜伏站点中**已有完整调用栈**的两个：

- `include/agenticdsl/pdk/agent_loops/plan_execute_loop.h:208` (plan_phase)
- `include/agenticdsl/pdk/agent_loops/plan_execute_loop.h:254` (verify_phase)

**用户原始关注**："交了 LLM 后延时会变长，需要测试来验证能工作"。PlanExecuteLoop
是 PDK Agent 编排的核心三阶段循环（Plan → Execute → Verify），未验证真实 deepseek
行为下：
- `plan_phase` LLM 是否产出**合法 DSL**（DSLEngine::continue_with_generated_dsl 解析）
- `verify_phase` LLM 是否响应**含 "yes"**（大小写不敏感）→ 循环正确终止

**当前唯一测试** (`tests/test_plan_execute_restart.cpp`) 用 `MockLLMProvider::enqueue_response`
覆盖 restart 语义（3 cases），**未在真实 LLM 下验证** — mock 响应手工构造，无法保证
LLM 真实输出能被 DSLEngine 解析。

## What Changes

### Scope: 真实 LLM 测试用例（3 cases）

扩展 `tests/test_plan_execute_realllm.cpp`（新建）：

1. **C.1**: `plan_phase` 真实 deepseek → 合法 DSL → `continue_with_generated_dsl` 解析成功
2. **C.2**: `verify_phase` 真实 deepseek → 含 "yes" (大小写不敏感) → loop 终止成功
3. **C.3**: end-to-end 真实 deepseek → run("compute 2+3", ctx) → success（plan+execute+verify
   全过, 验证链路）

### 关键技术约束

**A. 依赖 `fix-generation-request-model-default`** (P0 follow-up):
- `plan_phase` / `verify_phase` 是 5 个潜伏面之一, 不修 model 遮蔽则真实 LLM 必撞墙
- 本 change **不修生产代码** (scope out, 属 sibling fix-up change)
- 本 change 在测试代码内**显式设 `req.params.model = model_name`** 作为 workaround
  (mock 不受影响, real LLM 经 SerializingDecorator fallback config_.model)
- 完成时间线: 本 change 在 `fix-generation-request-model-default` ship 后实施

**B. 依赖 `fix-cloud-adapter-multithreading`** (P0 follow-up):
- 本 change 是**单线程** (PlanExecuteLoop run() 在 user thread) → 多线程 SIGSEGV 不触发
- 但 verify_phase 失败 retry 涉及同 provider 多次调用 (serial) — 走 SerializingDecorator
  兼容路径 (无 race 但串行)
- 因此**不强制依赖** multithread 修复

**C. test helper 复用**:
- `tests/test_helpers/real_llm_env.h` 已 ship (Phase 0 of real-llm-core-coverage)
- `agenticdsl::test::require_real_llm_env()` + `real_llm_env_skipped()` 标准 short-circuit
- `agenticdsl::test::real_llm_provider()` 构造 provider → set_llm_provider 注入

### Scope Boundaries (In)

- ✅ `tests/test_plan_execute_realllm.cpp` 新建 3 cases
- ✅ 每个 case 含 skip short-circuit (`real_llm_env_skipped()` early return)
- ✅ 显式设 `req.params.model` (规避默认遮蔽, 即使 model 修复 ship 也保持)
- ✅ 诊断输出 (`std::cerr` 打印 plan / verify 响应, 失败时定位)
- ✅ 复跑 `tests/test_plan_execute_restart.cpp` 零回归 (mock 路径不变)
- ✅ **2 站点 model 遮蔽修复** (plan_execute_loop.h:208 plan_phase + :254 verify_phase,
  Wave 1 #1 Oracle 漏掉的 2 个潜伏面, 实施时发现立即扩展 scope)

### Scope Boundaries (Out)

- ❌ ~~不修改 `plan_execute_loop.h` 生产代码~~ **覆盖**: 见 §Scope Boundaries (In)
  第 6 项 (scope 扩展记录, Oracle ship-gate ses_f7cdf267bffeddRNuPpIcdOZd0 修正)
- ❌ 不修改 `verify_phase` "yes" 判定逻辑 (大小写不敏感 substring, 现有契约)
- ❌ 不实现 `/model` 运行时 provider 切换 (属 `chat-model-switch-real`)
- ❌ 不修 token passthrough (verify_phase 当前传 `token`, plan_phase 也是)
- ❌ 不实施多线程 PlanExecuteLoop (Phase C 单线程, 并发属后续)

## Scope

**新增**:
- `tests/test_plan_execute_realllm.cpp` (3 cases, ~150 行)
- 改动 `tests/CMakeLists.txt` (1 行, 新 target, `file(GLOB)` 自动注册, 无 manual)

**总计 ctest 影响**: +3 cases, baseline 224 → ~227 (无 regression)

**CI 影响**: 全部 `[realllm]` tag, `HYDRAFORGE_SKIP_REAL_LLM=1` 默认 skip; 无 key 时 FAIL
(硬门槛已 ship by Phase 0 helper)

**Non-goals**:
- ❌ 不修 model 遮蔽 (属 `fix-generation-request-model-default`)
- ❌ 不修 plan/verify 多线程 race (本 change 单线程)
- ❌ 不验证 plan_phase 的 LLM 输出**质量** (DSL 是否逻辑正确, 只验证能解析)

## Impact

**风险**: plan_phase LLM 可能产出无法解析的 DSL → test FAIL → 需要：
1. prompt 调整 (现有 prompt `"\nGenerate AgenticDSL markdown for /main subgraph:"` 不够明确)
2. 或 production prompt 改进 (PlanExecuteLoop prompt 设计 — 但属生产代码改动, scope out)

**缓解**: 
- 测试**断言宽松** (DSL 解析即可, 不验证内容) — 反映 LLM 输出不确定
- 失败时诊断输出 (std::cerr) 便于调试
- 至少 1 case 跑通即满足 spec 契约 ("A.2 真实 deepseek 下链路工作")

## 依赖

| 上游 | 内容 | 状态 |
|---|---|---|
| `real-llm-core-coverage` Phase 0+A ship | helper 自测 + CognitiveWorker 真实 LLM pattern | ✅ SHIPPED (commit afc2d1b + 117850c) |
| `fix-generation-request-model-default` | 修 plan_phase / verify_phase model 遮蔽 | ⏳ PENDING (P0 follow-up) |
| `fix-cloud-adapter-multithreading` | (可选) 多线程并发 | ⏳ PENDING (P0 follow-up, 但本 change 单线程不依赖) |

| 下游 | 内容 |
|---|---|
| `real-llm-core-coverage` Phase D/E/F/G ship | 后续 phase 验证 model 修复 + 串行化 |
| PDK Agent 用户 (loop_agent plugin) | 可用 PlanExecuteLoop 跑真实 deepseek |

## 升级触发 (Escalation)

若 Phase C 实施时发现:
- **plan_phase prompt 需要重写** (LLM 反复产出无法解析 DSL) → 开
  `improve-plan-execute-loop-prompt` 独立 change (生产代码)
- **verify_phase 判定逻辑需要改进** ("yes" 检测鲁棒性) → 同上
- **生产代码任一处需改** → 升级为前置 (本 change 不修生产代码)

## 验证标准

- `cmake --build build -j$(nproc)` 编译通过 (0 error, 0 warning)
- `tests/test_plan_execute_realllm.cpp` 3 cases:
  - C.1: plan_phase 真实 LLM 产出可解析 DSL — PASS (本地有 key)
  - C.2: verify_phase 真实 LLM 响应含 "yes" → loop 成功 — PASS
  - C.3: end-to-end "compute 2+3" 真实 LLM 全链路成功 — PASS (或 ≥1 case PASS
    + 剩余优雅 error, 反映 LLM 不可控)
- skip 模式: 3 cases SUCCEED short-circuit (CI 友好)
- 无 key + 无 skip: 3 cases FAIL (硬门槛, helper 自带)
- `tests/test_plan_execute_restart.cpp` 零回归
- 全量 `ctest -j$(nproc)` baseline 224 → 227 +3 PASS, 0 regression
- `openspec validate plan-execute-loop-realllm --strict` exit 0
- `tools/adr_lint.py` 0 errors
- `tools/docs_drift_audit.py` 0 CRITICAL drift

## 估时

| 阶段 | 内容 | 估时 |
|---|---|---|
| 1 | 读取 plan_execute_loop.h 完整 + 现有 restart 测试完整 | 15 min |
| 2 | 编写 `tests/test_plan_execute_realllm.cpp` 3 cases + helper + skip | 1 h |
| 3 | CMake 验证 + ctest | 15 min |
| 4 | 真实 deepseek 验证 (本地有 key) + 调试 prompt (若需要) | 30 min |
| 5 | commit + openspec validate + archive | 15 min |
| **Total** | | **~2.5 h** |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| plan_phase LLM 反复产出无法解析 DSL → 测试 FAIL | prompt 调整 + 断言宽松 (解析即可) + 诊断输出; 失败时记录作为 prompt 改进 follow-up |
| verify_phase LLM 不返回 "yes" (可能 "yes." / "Yes, success" / "YES") | 大小写不敏感 substring 已 ship; 但需确认 "yes" 子串匹配覆盖 |
| PlanExecuteLoop 多次 generate 串行调用累积延迟 (C.3 end-to-end 可能 30-60s) | wait_until 超时 120s 充足; 性能不是本 change 重点 |
| mock 路径 (`test_plan_execute_restart.cpp`) 因 set_llm_provider 改变 mock 行为 | 现有测试用 enqueue_response, 不依赖 set_llm_provider; 零回归 |
| model 遮蔽未修前实施 → 真实 LLM 必撞墙 | 依赖 `fix-generation-request-model-default` 先 ship; 或本 change 测试内显式 `req.params.model = model_name` 临时绕过 (推荐) |
