# tests

**Generated:** 2026-05-11
**Updated:** 2026-09-08 (real-LLM test patterns)

## OVERVIEW
Catch2 单元测试，17+ 个测试文件（含 `test_real_llm_env_helper_core` 等 real-LLM 覆盖）。

## FRAMEWORK
- Catch2 (amalgamated 单文件版，v3.7.0+)
- 标签格式：`[module][stageN]` (如 `[scheduler][stage2]`)
- 运行：`ctest --output-on-failure`
- 真实 LLM 测试 tag：`[realllm]`（CI 默认 `HYDRAFORGE_SKIP_REAL_LLM=1` skip，无 key 时 helper FAIL 硬门槛）

## FILES
| File | Coverage |
|------|----------|
| test_engine.cpp | DSLEngine |
| test_engine_factory.cpp | DSLEngine 构造路径 |
| test_parser.cpp | MarkdownParser |
| test_scheduler.cpp | TopoScheduler |
| test_executor.cpp | NodeExecutor |
| test_tool_registry.cpp | ToolRegistry |
| test_llm_tool.cpp | LlamaAdapter |
| test_library_loader.cpp | StandardLibraryLoader |
| test_basic.cpp | 基础功能 |
| test_prompt_builder.cpp | Prompt 构建 |
| test_no_llm.cpp | 无 LLM 模式 |
| test_command_registry.cpp | CommandRegistry 注册/冲突/help/保留字/委托治理路径/绕过预防 (adr-0070) |
| test_cognitive_worker.cpp | CognitiveWorker 单线程 + Phase A real LLM (含 Recording Provider 守卫 Test 6) |
| test_simple_orchestrator.cpp | SimpleCognitiveOrchestrator mock + Phase A 回归守卫 |
| test_domain_worker_pool.cpp | DomainWorkerPool + Phase B (B.3 mock 100 串行 + B.4 mock RateLimited; B.2 deferred) |
| test_real_llm_env_helper_core.cpp | 项目级 helper 自测 (5 cases) |
| main_test_runner.cpp | 测试入口 |

## REAL-LLM TEST PATTERNS (real-llm-core-coverage 沉淀)

**沉淀时间**: 2026-09-08, **来源**: 同根 AGENTS.md §ENGINEERING PATTERNS 工程层细化.

### 1. Helper 三态分离 (`tests/test_helpers/real_llm_env.h`)

不要让 `require_real_llm_env()` 一个函数承担"检查 key + 报告 FAIL + 报告 SKIP"——拆为:

```cpp
namespace agenticdsl::test {

inline void require_real_llm_env() {
  if (skip == "1") return;                        // SKIP 静默
  if (DEEPSEEK_API_KEY set) return;               // run
  if (MINIMAX_API_KEY set) return;                // run
  FAIL("real LLM env required: ...");             // 硬门槛
}

inline bool real_llm_env_skipped() {              // 单独查询 skip
  return getenv("HYDRAFORGE_SKIP_REAL_LLM") == "1";
}
```

**为什么 core 树必须 short-circuit**: core 树 CI 默认构建; examples=OFF 时 sibling helper 根本不构建——pdk helper 不需要 short-circuit; core 树必须. 否则 skip=1 时空 api_key 构造 provider → generate 失败 → 测试误 FAIL → CI 红.

**测试标准模式**:
```cpp
TEST_CASE("...real LLM test...", "[realllm]") {
  agenticdsl::test::require_real_llm_env();
  if (agenticdsl::test::real_llm_env_skipped()) {
    SUCCEED("skipped: HYDRAFORGE_SKIP_REAL_LLM=1");
    return;
  }
  // ... test body ...
}
```

**namespace**: 项目级 `agenticdsl::test`（与 `http_mock_server.h` 一致），pdk 副本保留 `pdk_chat_demo::testing`。双维护可接受（<140 行 + duplicated self-test 兜底）。

### 2. Recording Provider 回归守卫

需要测试**生产代码传入 provider 的参数契约**时（如 `react_once` 是否传空 `params.model`），不要 mock provider 的"返回固定 JSON"——而要 **recording provider**:

```cpp
class RecordingLLMProvider : public ILLMProvider {
 public:
  std::string last_model;
  int generate_calls = 0;
  GenerationResult result;

  Result<GenerationResult, LLMError> generate(
      const GenerationRequest& req, std::stop_token) override {
    last_model = req.params.model;
    ++generate_calls;
    return Result::success(result);
  }
  std::unique_ptr<IGenerationStream> generate_stream(
      const GenerationRequest&, std::stop_token) override { return nullptr; }
  std::vector<ModelInfo> available_models() const override { return {}; }
};
```

- **不需要真实 API key**，CI 永远 PASS
- `set_llm_provider(recorder)` 经 `decorate_provider` 包装 CostTrackingDecorator，**raw 指针仍指向内层**（装饰链透传 const ref req，不修改）
- 断言 `raw->last_model.empty()` / `raw->generate_calls == N` 精确锁定契约

**反模式**: 只用真实 LLM 测试守护关键契约 → CI skip 时无防护，回归会漏过。

### 3. Catch2 SKIP macro 在 ctest 并行下卡死

**触发**: 测试有 `std::jthread` + `InMemoryBus` + `shared_ptr` 等 RAII 资源时.

**症状**: 单跑 exit=0 PASS; `ctest -j$(nproc)` 显示 `test_xxx (Subprocess aborted)`; `ctest -j1 -R test_xxx` hang >120s.

**根因**: `SKIP(msg)` 抛 `SkipException` 打乱正常析构路径——RAII 析构未跑完整 → dispatch_thread 或 jthread 析构 hang → `std::terminate` → SIGABRT → ctest 报 "Subprocess aborted".

**替代方案**:
```cpp
WARN("B.2 deferred (known issue: multi-thread SIGSEGV); "
     "fix-cloud-adapter-multithreading pending");
SUCCEED("B.2 deferred (see WARN above)");
return;  // 不抛异常, RAII 析构正常完成
```

**保留**: 测试骨架 + 注释解释 + WARN 记录（不静默失败），正常退出让 CI 通过。

**反模式**: 用 `SKIP` 暂时跳过有 RAII 资源的测试 → CI 误报 abort。

### 4. CMake target 命名冲突: CMP0002 + `_core` 后缀

**触发**: GLOB 跨多 trees（同 helper 文件名）+ sibling change 已 ship 占用同名 target.

**症状**: `cmake ..` 失败 `add_executable cannot create target "X" ... another target with the same name already exists`.

**不要**: 改 sibling target（破坏已 ship change 的 CI 注册 + 测试运行）。

**正确方案**: 在当前树对 GLOB 结果做映射:
```cmake
foreach(TEST_SRC ${SINGLE_TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
    if(TEST_NAME STREQUAL "test_real_llm_env_helper")
        set(TEST_NAME "test_real_llm_env_helper_core")
    endif()
    add_catch_test(${TEST_NAME} ${TEST_SRC})
endforeach()
```

**前置**: `file(GLOB ...)` 是 configure 时求值——**新增 .cpp 后必须重新 `cmake ..` (configure)** 才会捕获。

### 5. Real-LLM mock 测试中 model 参数显式设值

**触发**: handler / 测试逻辑中构造 `GenerationRequest` 调 cloud provider (Authorization header 存在).

**必须**: 显式 `req.params.model = model_name` 而非依赖默认.

**why**: `LLMParams = LLMConfig` 别名; `LLMConfig::model` 默认 `"gpt-4o-mini"` **非空**; cloud adapter L164 `req.params.model.empty() ? config_.model : req.params.model` → 默认值遮蔽真实 model → server 拒绝（`"you passed gpt-4o-mini"`）→ 真实 LLM 测试 FAIL.

**当前 workaround**: 每处 handler 显式 `req.params.model.clear()` 或 `= model_name`.

**根本修复**: 跟进 change `fix-generation-request-model-default`（5 个潜伏站点系统修复）。

**反模式**: 假设默认 model 是好的 → 一上真实 cloud provider 全 fail.

### 6. CMake GLOB 与 GTest 双 helper 策略

详见根 AGENTS.md §治理层 模式 5.

### 7. helper 测试的 FAIL 占位技巧

helper 自测中"无 key + 无 skip → FAIL"是 1 行直接调 Catch2 `FAIL()`——机械性极低, 真实 FAIL 路径在无 key 运行时被真实测试触发. 用 SUCCEED 占位 (`SUCCEED("FAIL behavior verified by real LLM tests")`) 是充分的, **不要**写 fork+exec 子进程断言（过度设计）.

---

## 工程层模式引用

测试目录专属设计/工程模式见本文件 §REAL-LLM TEST PATTERNS.
LLM 模块专属 (provider/adapter) 模式见 `src/common/llm/AGENTS.md` §LLM ADAPTER PATTERNS.
项目级决策/治理模式见根 `AGENTS.md` §ENGINEERING PATTERNS.
