# fix-skill-interpreter-token-and-timeout — Tasks

## 1. 实施
- [x] 1.1 skill_interpreter.h: run 加 std::stop_token = {} 形参
- [x] 1.2 skill_interpreter.cpp: Impl::run 加 token 形参, 转发至 ipc_loop_and_wait
- [x] 1.3 skill_interpreter.cpp: ipc_loop_and_wait 加 token = {} 形参
- [x] 1.4 skill_interpreter.cpp: while loop 顶部加 token.stop_requested() 检查 + SIGKILL + ErrorCode::Abort
- [x] 1.5 skill_interpreter.cpp: public wrapper run 加 token 形参
- [x] 1.6 test_skill_interpreter.cpp: Test 7.8b 新增
- [x] 1.7 (BONUS) 修复 3 处 pre-existing 错误 expectation (7.2/7.8/7.19)

## 2. 验证
- [x] 2.1 cmake --build PASS
- [x] 2.2 test_skill_interpreter 19/19 PASS (48 assertions)
- [x] 2.3 全量 ctest skip mode PASS
- [x] 2.4 adr_lint PASS
- [x] 2.5 docs_drift_audit: 0 DRIFT items

## 3. Commit + Oracle ship-gate
- [x] 3.1 commit bafce05 (fix+tests)
- [x] 3.2 scaffold commit (本文档 + 4 文件)
- [ ] 3.3 Oracle ship-gate (background)
- [ ] 3.4 Oracle APPROVE 后 archive

## 4. 文档同步 (待 archive 后)
- [ ] 4.1 active-status.md 更新
- [ ] 4.2 ctest 计数 (新 Test 7.8b → 235)