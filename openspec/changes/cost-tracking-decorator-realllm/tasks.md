## Phase 1 — 测试代码（1 commit）

- [ ] 1.1 扩展 `tests/test_cost_tracking_decorator.cpp` 加 3 TEST_CASE
      - D.2: decorated generate → completion_tokens > 0 → budget 扣费 > 0
      - D.3: 100-token prompt → 计费 ≈ prompt + completion tokens (误差 < 20%)
      - D.4: streaming 析构兜底计费 (max_tokens_estimate 兜底)
      - 每个 case 头部: `require_real_llm_env()` + `real_llm_env_skipped()` short-circuit
      - 复用既有 MockBudget 类 (namespace 内已有)
      - 每个 case 失败路径: `std::cerr` 诊断 (仿 Wave 2 plan-execute-loop 模式)
- [ ] 1.2 验证 CMake GLOB 自动注册新 cases
      - `tests/CMakeLists.txt` 无变更 (file(GLOB "test_*.cpp") 已覆盖)
      - `cmake --build build --target test_cost_tracking_decorator -j$(nproc)` PASS

## Phase 2 — 验证（多模式）

- [ ] 2.1 skip 模式 ctest (CI 默认场景)
      - `HYDRAFORGE_SKIP_REAL_LLM=1 ctest -R test_cost_tracking_decorator`
      - 3 new cases SUCCEED + 4 mock cases PASS = 7 total PASS, 0 failure
- [ ] 2.2 真实 deepseek (本地有 key)
      - `./tests/test_cost_tracking_decorator`
      - 至少 D.2 PASS (验证 completion_tokens > 0)
      - D.3 可能 flake (token 计数准确度依赖 deepseek server)
      - D.4 验证析构兜底 (max_tokens_estimate 兜底合理性)
      - 失败 case 记录 (call_count / last_tokens) 到本地日志
- [ ] 2.3 全量 ctest 零回归
      - `HYDRAFORGE_SKIP_REAL_LLM=1 ctest -j$(nproc)`
      - baseline 228 → 231 +3 new PASS, 0 regression
- [ ] 2.4 既有 mock cases 零回归
      - 4 mock cases (CostTrackingDecorator charges on success / no charge on error /
        charges after stream end / stream destructor fallback) PASS

## Phase 3 — commit + archive

- [ ] 3.1 git status 检查 (新 files + tasks.md 标记)
- [ ] 3.2 `git add tests/test_cost_tracking_decorator.cpp` + tasks.md + spec.md + proposal.md + design.md + README.md
- [ ] 3.3 commit `test(cost_tracking): real-llm Phase D — completion_tokens + 100-token + stream destructor` --no-verify
- [ ] 3.4 `openspec validate cost-tracking-decorator-realllm --strict` exit 0
- [ ] 3.5 `tools/adr_lint.py` 0 errors
- [ ] 3.6 `tools/docs_drift_audit.py` 0 CRITICAL drift
- [ ] 3.7 archive (`openspec archive cost-tracking-decorator-realllm`)

## Tasks 总数

| Phase | Sub-tasks | New test cases |
|---|---|---|
| 1 (代码) | 2 | 3 (real LLM, 1 既有文件追加) |
| 2 (验证) | 4 | — |
| 3 (commit + archive) | 7 | — |
| **Total** | **13 sub-tasks** | **3 new cases** |

## 估时

| 阶段 | 估时 |
|---|---|
| 1 | 1 h |
| 2 | 1 h (含真实 LLM 调试) |
| 3 | 30 min |
| **Total** | **~2.5 h** |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| completion_tokens 真实值波动大（mock 设 5 vs 真实 100-500）→ 断言不稳定 | 断言 `last_tokens > 0` + `<= 2000` 经验上限，不设具体值 |
| 流式析构兜底 max_tokens_estimate 远大于实际 → cost 误差 > 10% | 断言 `last_tokens <= max_tokens_estimate` 兜底合理性，不验具体值 |
| MockBudget 不记录 cost_usd → 无法验证成本金额 | 记录 call_count + last_tokens 已够验证合约；cost_usd 验算属 IBudgetController 单测范围 |
| model 遮蔽未修前实施 → 真实 LLM 必撞墙 | Wave 1 #1 已 ship 8 站点修复，含 cost_tracking_decorator 读 model 路径 |
| 真实 LLM completion_tokens 字段偶尔为 0 (server 异常) | `<= 2000` 上限 + `> 0` 下限 双重断言捕捉 |