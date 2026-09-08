## Phase 1 — 代码修复（4 commits, 5 站点 + 1 已 ship）

- [ ] 1.1 `node_executor.cpp:356` (GenerateSubgraphNode ll_call 路径)
      — 加 `req.params.model.clear();` + 注释解释为何不是多余的
- [ ] 1.2 `node_executor.cpp:574` (YieldNode 流式)
      — 加 `req.params.model.clear();` + 同注释模板
      — 注: YieldNode 还存在 token={} 问题 (属 `fix-yield-node-token-passthrough`), 本 change 仅修 model 部分
- [ ] 1.3 `skill_interpreter.cpp:657-659` (IPC llm_generate)
      — 加 `gen_req.params.model.clear();` + 同注释模板
- [ ] 1.4 `src/core/context_compactor.cpp:60` (摘要)
      — 加 `req.params.model.clear();` + 同注释模板
- [ ] 1.5 `gepa_loop.cpp:115` (GEPA 反射)
      — 加 `request.params.model.clear();` + 同注释模板
- [ ] 1.6 (已 ship) `simple_orchestrator.cpp:118` — 由 `real-llm-core-coverage` Phase A 完成 (commit `afc2d1b`), 不重做

## Phase 2 — 回归守卫（5 单元 + 1 集成）

每个站点 1 个 recording provider 单元测试, 验证传给 generate()/generate_stream() 的 req.params.model 为空:

- [ ] 2.1 `tests/test_node_executor.cpp` 加 TEST_CASE "GenerateSubgraphNode passes empty model to provider"
      — Oracle P1-1 风格 RecordingLLMProvider, 断言 last_model.empty() + generate_calls==1
- [ ] 2.2 `tests/test_node_executor.cpp` 加 TEST_CASE "YieldNode passes empty model to provider"
      — 注: stream 路径, recorder 也要实现 generate_stream (记录 last_model in stream path)
      — token={} GAP 记录 (`WARN("token passthrough deferred to fix-yield-node-token-passthrough")`)
- [ ] 2.3 `tests/test_skill_interpreter.cpp` 加 TEST_CASE "Skill llm_generate IPC passes empty model"
      — mock IPC 模式 (父进程模拟 child), recorder 在父进程
- [ ] 2.4 `tests/test_context_compactor.cpp` (新建或扩展) 加 TEST_CASE "ContextCompactor compact passes empty model"
      — 注: 若无基础 test_context_compactor.cpp, 先建空壳
- [ ] 2.5 `tests/test_gepa_loop.cpp` (新建或扩展) 加 TEST_CASE "GEPA reflection passes empty model"
      — 注: 若无基础 test_gepa_loop.cpp, 先建空壳
- [ ] 2.6 `scripts/check-model-default-cleared.sh` (新建) — 静态契约检查
      — grep 5 站点附近 `req.params.model.clear()` (或 `request.params.model.clear()`)
      — 退出码: 0 = 全部找到, 1 = 漏站点
      — 集成到 CMake (CTest add_test) 或 scripts/sprint-closeout.sh

## Phase 3 — 验证 + archive

- [ ] 3.1 `cmake --build build -j$(nproc)` 编译通过 (0 error, 0 warning)
- [ ] 3.2 `ctest -j$(nproc)` baseline 224 → 229 +5 new (or +6 含 2.6 脚本), 0 regression
- [ ] 3.3 `openspec validate fix-generation-request-model-default --strict` exit 0
- [ ] 3.4 `tools/adr_lint.py` 0 errors
- [ ] 3.5 `tools/docs_drift_audit.py` 0 CRITICAL drift
- [ ] 3.6 `git status` 检查 + 5 commits (Phase 1 站点) + 1 commit (Phase 2 测试)
- [ ] 3.7 archive (`openspec archive fix-generation-request-model-default`)

## Tasks 总数

| Phase | Sub-tasks | New test cases |
|---|---|---|
| 1 (代码) | 5 | — |
| 2 (守卫) | 6 | 5 (unit) + 1 (脚本静态) |
| 3 (验证) | 7 | — |
| **Total** | **18 sub-tasks** | **~6 new artifacts** |

## 估时

| Phase | 估时 |
|---|---|
| 1 | 45 min (5 行 + 5 注释 + 验证编译) |
| 2 | 2 h (5 unit tests + 1 脚本) |
| 3 | 20 min |
| **Total** | **~3 h** |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| 修复 1.2 YieldNode 同时有 token={} GAP, 测试可能因 token 缺失 FAIL | WARN + SUCCEED 模式 (tests/AGENTS.md 模式 3); test 仅验证 model 契约, 不验证 cancellation |
| 修复 1.5 GEPA loop 用 `GenerationRequest request("...")` 构造 ctor, 可能不暴露 params 给外部 | grep 上下文确认 ctor 是否默认 params; 若 ctor 已设, 改 clear() |
| 集成脚本 grep 误报 (false positive 匹配其他 req) | 精准 grep (行号范围 + 注释模板匹配) |
| Phase D (Cost) assertion 不容忍 model="" | tasks.md §D 阶段 note 提前提醒 |
| recording provider 与 ILLMProviderDecorator 包装 | 同 real-llm-core-coverage Phase A.5, 装饰链透传 const ref req 已验证 |
