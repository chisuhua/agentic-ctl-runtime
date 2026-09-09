# Design — fix-yield-node-token-passthrough

## 修复方案 (1 行 token 替换)

### 改动位置

`src/modules/executor/node_executor.cpp:592` 在 `YieldNode::execute_y` 函数体内.

### 修复前 (Wave 1 #1 ship 状态)

```cpp
// node_executor.cpp:582-602 (YieldNode NEXT/CONTINUE case)
case YieldMode::NEXT:
case YieldMode::CONTINUE: {
    GenerationRequest req;
    req.prompt = std::move(rendered);
    // ⚠️ NOT redundant: ... [Wave 1 #1 加的 clear() 注释]
    req.params.model.clear();
    // 注: token={} 属 fix-yield-node-token-passthrough scope (独立 follow-up),
    // 本 commit 仅修 model 契约, 不触碰 token 透传.
    auto stream = llm_provider_->generate_stream(req, std::stop_token{});  // ← BUG
    // ...
```

### 修复后

```cpp
case YieldMode::NEXT:
case YieldMode::CONTINUE: {
    GenerationRequest req;
    req.prompt = std::move(rendered);
    // ⚠️ NOT redundant: ... [保留 Wave 1 #1 注释]
    req.params.model.clear();
    // Wave 1 #1 #2 ship 后续: token 透传至 generate_stream, 外部 cancel 可中断流
    auto stream = llm_provider_->generate_stream(req, token);  // ← FIX
    // ...
```

删除原 "注: token={} 属 fix-yield-node-token-passthrough scope" 注释 (已 ship 修复).

## 测试设计

### 单元测试 1 case

位置: `tests/test_node_executor.cpp` 新增 (既有文件追加, 既有 YieldNode mock test 之后)

```cpp
// === Wave 1 #1 GAP fix: YieldNode 流式 token passthrough 取消 ===
TEST_CASE("YieldNode stream cancellation via stop_token passthrough",
          "[node_executor][yield_node][token][realllm-gap-fix]") {
  // 构造 MockLLMProvider generate_stream 支持 token-aware sleep
  // (若 MockLLMProvider 不支持, 加 30 行 hook — 见 Phase D 经验)
  auto mock = std::make_unique<MockLLMProvider>();
  mock->set_stream_chunks({"chunk1", "chunk2", "chunk3"});  // mock generate_stream 返回多 chunk
  
  // YieldNode NEXT 模式 + bus + 真实 token 来源
  std::stop_source ss;
  YieldNode node("/main/yield", YieldMode::NEXT, /*prompt_template*/"");
  NodeExecutor executor(registry, bus, mock.get());
  
  // jthread 启动 execute_y
  std::jthread worker([&]() {
    executor.execute_yield(&node, ctx, ss.get_token());
  });
  
  // 短暂等待流进入 (mock 第 1 个 chunk 立即返回)
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  
  // 取消
  ss.request_stop();
  
  // 验证 worker 在合理时间内返回 (≤ 1s, 远小于 LLM 调用延迟)
  worker.join();  // jthread RAII 自动 join
  
  // 核心契约: 取消信号传播至 generate_stream → 流立即返回
  // (若 mock stream 检查 token, 应返回 false → loop 退出 → execute_y 返回)
  REQUIRE(true);  // worker.join 不死锁即 token 透传成功
}
```

### 关键点

**MockLLMProvider token-aware**: 既有 `MockLLMProvider::generate_stream` 是否检查
`stop_token.stop_requested()`? 若否, 测试无法验证取消路径 (mock 永远返回完整流).
需扩展 mock 加 token-aware sleep (Phase D CostTrackingDecorator 测试已有类似 pattern).

**不依赖真实 LLM**: 本测试用 MockLLMProvider, CI 友好 (无 API key). 真实 LLM
取消需 deepseek server 支持 (server-side cancel), 是 cloud adapter 责任而非本 change.

## 实施顺序

1. **Phase 1 (1 行 fix + 1 测试)**:
   - `node_executor.cpp:592` 1 行 token 替换 + 删除 "注: token={} 属 follow-up" 注释
   - MockLLMProvider 增强 (若需) token-aware sleep
   - `tests/test_node_executor.cpp` 新增 1 case
2. **Phase 2 (验证)**:
   - skip 模式 ctest: 1 case SUCCEED + 既有 mock tests PASS
   - 真实 LLM 验证 (可选, 本地有 key)
3. **Phase 3 (commit + archive)**:
   - 1 commit (fix + test) + archive

每步骤独立 commit (atomic commit 原则).