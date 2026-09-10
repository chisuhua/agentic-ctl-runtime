## 1. 实施

- [ ] 1.1 skill_interpreter.cpp: dispatch 加 `std::stop_token token = {}` 形参 (line 600)
- [ ] 1.2 skill_interpreter.cpp: dispatch_llm_generate 路径调用改为 `dispatch_llm_generate(req, cap, token)` (line 609)
- [ ] 1.3 skill_interpreter.cpp: dispatch_llm_generate 定义加 `std::stop_token token = {}` 形参 (line 668)
- [ ] 1.4 skill_interpreter.cpp: THE FIX — `llm_->generate(gen_req, std::stop_token{})` → `llm_->generate(gen_req, token)` (line 686)
- [ ] 1.5 skill_interpreter.cpp: ipc_loop_and_wait 调用 dispatch 处传入 token 形参
- [ ] 1.6 test_skill_interpreter.cpp: 新增 Test 7.8d "dispatch_llm_generate forwards external stop_token"

## 2. 验证

- [ ] 2.1 cmake --build 零 error + 零 warning
- [ ] 2.2 test_skill_interpreter 21/21 PASS (含新 7.8d, 既有 7.8b/7.8c 不变)
- [ ] 2.3 全量 ctest skip mode 228/228 PASS 零回归
- [ ] 2.4 adr_lint PASS
- [ ] 2.5 docs_drift_audit: 0 DRIFT items
- [ ] 2.6 openspec validate --strict PASS

## 3. Commit + Oracle ship-gate

- [ ] 3.1 commit fix+tests (production code + test_skill_interpreter.cpp)
- [ ] 3.2 commit scaffold (本文档 + 3 文件)
- [ ] 3.3 dispatch Oracle ship-gate (background)
- [ ] 3.4 Oracle APPROVE → `openspec archive fix-skill-interpreter-dispatch-llm-token --yes`

## 4. 文档同步 (待 archive 后)

- [ ] 4.1 active-status.md 更新 (Wave 4 follow-up #2 archived 记录)