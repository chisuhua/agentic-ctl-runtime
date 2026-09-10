## 1. ErrorCode enum 扩展

- [ ] 1.1 src/core/types/tool_result.h: ErrorCode enum 末尾追加 `Cancelled` 值 (紧接 `InvalidParams` 之后)
- [ ] 1.2 src/core/types/tool_result.cpp: `error_code_to_string()` 加 `case ErrorCode::Cancelled: return "Cancelled";`
- [ ] 1.3 src/core/types/tool_result.cpp: `string_to_error_code()` 加 `if (s == "Cancelled") return ErrorCode::Cancelled;`
- [ ] 1.4 (verify) 验证所有 ErrorCode switch consumer 编译期 -Wswitch 零 warning

## 2. llm_error_to_error_code 修改

- [ ] 2.1 src/modules/cognitive/simple_orchestrator.cpp: 修改 `case LLMError::Code::Cancelled` 从 `return ErrorCode::Unknown` → `return ErrorCode::Cancelled`
- [ ] 2.2 (verify) 其他 case (NetworkError/RateLimited/ServerError/AuthenticationError/ContextOverflow/InvalidRequest/Unknown) 保持不变

## 3. 测试

- [ ] 3.1 tests/test_simple_orchestrator.cpp: 新增 Test 8 "LLMError::Cancelled maps to ErrorCode::Cancelled"
  - [ ] 3.1.1 RecordingLLMProvider 新增 `simulate_cancellation` 字段 (default false, additive, 不影响其他 tests)
  - [ ] 3.1.2 RecordingLLMProvider::generate 修改 — 若 `simulate_cancellation == true` → 返回 `Result::failure(LLMError{Code::Cancelled, ...})`
  - [ ] 3.1.3 Test 8 主体: simulate_cancellation=true → orch.process → 断言 captured.error_code == ErrorCode::Cancelled
- [ ] 3.2 (verify) test_simple_orchestrator 全 PASS (含既有 7 个 tests + 新 Test 8)
- [ ] 3.3 (verify) test_tool_result_strings.cpp 若存在则 PASS (新增 string 映射 case)

## 4. 全量验证

- [ ] 4.1 cmake --build 零 error + 零 warning
- [ ] 4.2 HYDRAFORGE_SKIP_REAL_LLM=1 ctest 228/228 PASS 零回归
- [ ] 4.3 openspec validate fix-cancel-errorcode-semantics --strict PASS
- [ ] 4.4 adr_lint PASS
- [ ] 4.5 docs_drift_audit: 0 DRIFT items
- [ ] 4.6 check-model-default-cleared 8/8 OK

## 5. Commit + Oracle ship-gate

- [ ] 5.1 commit fix+tests (production code + test changes)
- [ ] 5.2 commit scaffold (本文档 + design.md + specs/spec.md)
- [ ] 5.3 dispatch Oracle ship-gate (background)
- [ ] 5.4 Oracle APPROVE → `openspec archive fix-cancel-errorcode-semantics --yes`

## 6. 文档同步 (待 archive 后)

- [ ] 6.1 active-status.md 更新 (新增 fix-cancel-errorcode-semantics archived 记录)