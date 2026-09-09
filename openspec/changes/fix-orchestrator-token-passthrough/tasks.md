# fix-orchestrator-token-passthrough — Tasks

## 1. 实施
- [x] 1.1 simple_orchestrator.h: process 加 std::stop_token = {} 形参
- [x] 1.2 simple_orchestrator.h: react_once 加 std::stop_token = {} 形参
- [x] 1.3 simple_orchestrator.cpp: process 转发 token 至 react_once (line 91)
- [x] 1.4 simple_orchestrator.cpp: react_once 转发 token 至 llm_->generate (line 125)
- [x] 1.5 test_simple_orchestrator.cpp: RecordingLLMProvider 新增 last_token_stop_requested 字段
- [x] 1.6 test_simple_orchestrator.cpp: Test 7 forwards stop_token to LLM provider

## 2. 验证
- [x] 2.1 cmake --build PASS
- [x] 2.2 test_simple_orchestrator 3/3 ctest PASS (含 Test 7)
- [x] 2.3 test_cognitive_worker 零回归
- [x] 2.4 test_context_compactor 零回归 (依赖 RecordingLLMProvider)
- [x] 2.5 全量 ctest skip mode PASS

## 3. Commit + Oracle ship-gate
- [x] 3.1 commit 4598fda (fix+tests)
- [x] 3.2 scaffold commit (本文档 + 4 文件)
- [ ] 3.3 Oracle ship-gate (background)
- [ ] 3.4 Oracle APPROVE 后 archive

## 4. 文档同步 (待 archive 后)
- [ ] 4.1 active-status.md 更新 (Wave 4 进度)
- [ ] 4.2 ctest 计数 (新 Test 7 → 234)
