# fix-cloud-adapter-multithreading

**Status**: Draft (scaffold)

## Scope

修复 CloudLLMAdapter 多线程 SIGSEGV（gdb 已实证）: N≥2 worker 并发 + Authorization
header + https 三者同时出现时 httplib::Client::Post 内部 create_client_socket 栈
corruption。

## Why

`real-llm-core-coverage` Phase B 实施时（commit `c0cb522`）发现:

| 路径 | 结果 |
|---|---|
| 单线程 + 真实 deepseek (A.2 ship) | ✅ PASS |
| 1 worker + 真实 deepseek (B.2 pool(1)) | ✅ PASS |
| 4 worker + mock provider (B.3, B.4) | ✅ PASS |
| 4 worker + 真实 deepseek (B.2 pool(4)) | ❌ **SIGSEGV** |
| `HttpLLMAdapter` 单独模式 (test_http_adapter) | ✅ PASS |

gdb backtrace 定位: `do_post` → `httplib::Client::Post` → `ClientImpl::send_` →
`create_client_socket` → 构造 `std::function<void(int)>` → 跳到空地址 (`0x11`) →
SIGSEGV。`socket_options_` 栈 corruption。

## 修复方案

默认方案 A: 工厂层 `LLMProviderFactory::create` 为 cloud 路径注入
`SerializingDecorator` (`std::mutex` + `std::condition_variable` 串行化
generate/generate_stream)，根除并发 race。

**只包装 cloud provider**（deepseek/openai/anthropic/qwen/...），不包装 mock
/llama — 不影响非 cloud 路径性能。

## 验证

- `openspec validate fix-cloud-adapter-multithreading --strict` exit 0
- 4 unit + 1 integration + 1 stress test PASS
- 真实 deepseek stress (8 worker × 20 tasks) 零 SIGSEGV
- 解锁 `real-llm-core-coverage` Phase B B.2 (移除 SKIP)

## 依赖

- **被依赖**: `real-llm-core-coverage` Phase B B.2, Phase E (Skill IPC 多 worker),
  Phase G (ContextCompactor 多 worker), pdk_chat_demo 多 agent 真实 LLM 场景

## follow-up (不阻塞本 change ship)

- ADR-XXXX "Cloud adapter threading model" — 升级到方案 B (根因修复: OpenSSL
  3.0 + httplib 最新版 + 移除 SerializingDecorator 恢复并发)
- `fix-generation-request-model-default` (并行, 不同问题域)

## Artifacts

- `proposal.md` — Why / 3 方案对比 / 默认方案 A
- `design.md` — SerializingDecorator 设计 + factory 集成 + 测试设计
- `tasks.md` — 17 sub-tasks across 3 phases
- `specs/fix-cloud-adapter-multithreading/spec.md` — 4 ADDED Requirements
