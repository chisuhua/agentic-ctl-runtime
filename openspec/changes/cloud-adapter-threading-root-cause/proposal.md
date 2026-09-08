## Why

`fix-cloud-adapter-multithreading` (Wave 1 #2) 通过工厂层 `SerializingDecorator`
(mutex + cv 串行化 generate/generate_stream) 规避了 CloudLLMAdapter 多线程
SIGSEGV — 但**这是规避, 不是根因修复**:

**SIGSEGV 根因候选**（gdb backtrace 实证, commit c0cb522 Phase B ship）:
1. **OpenSSL `SSL_CTX` 多线程初始化竞态** — httplib 或 OpenSSL 默认 SSL_CTX
   不是 per-thread, 多线程同时首次建立 SSL 连接触发 `SSL_new`/
   `SSL_CTX_use_certificate` 等内部状态 corruption (OpenSSL 1.1+ 自带
   线程安全, 但需正确链接 + 锁初始化)
2. **httplib Authorization header 栈处理 bug** — httplib 内部对某些 header
   value 的栈分配在多线程下 corruption (gdb backtrace 显示 `socket_options_`
   栈 corruption, `std::function<void(int)>` 跳到空地址 0x11)

**SerializingDecorator 的代价**:
- 牺牲并发 LLM 调用 (N worker → 1 LLM call at a time)
- 单 worker: 零影响
- 4 worker 并发: 4× 串行 (vs 期望 1× 并行), ~3s/call × 4 = 12s (vs ~3s 并行)
- worker 池整体价值 (非 LLM 工作并发) 影响有限

**风险 (无根因修复)**:
- 性能税永久固化 (设计师未来提 "为什么 N worker 不并发?" 时无历史答案)
- httplib/OpenSSL 漏洞在其他场景可能复现 (Skill IPC 多 worker / ContextCompactor
  多 worker / pdk_chat_demo 多 agent 真实 LLM)
- 项目依赖 httplib + OpenSSL 升级路径未建立

## What Changes

### Scope: 真根因修复 (ADR 追踪物, 非当前实施)

本 change **仅创建追踪物 + 记录根因诊断路径**, 不实际实施升级方案
(根因修复独立估时 1-3 周排查 + 升级周期, 不阻塞 Wave 1 #2 ship).

实施本 change 时将包含:
- 升级 OpenSSL 1.1+ → 3.0 (线程安全 + 现代 API)
- 升级 httplib 到 upstream 最新版 (含可能 patch)
- 移除 `SerializingDecorator` (真支持并发后不再需要)
- 验证 Phase B B.2 4 worker + Phase E Skill IPC 多 worker + Phase G
  ContextCompactor 多 worker 真并发工作
- 保留 SerializingDecorator 作为 fail-safe (OPT-IN, 不默认启用)

### 升级路径预估

| 步骤 | 内容 | 估时 |
|---|---|---|
| 1 | OpenSSL 3.0 集成 (find_package + 链接) | 1-2 天 |
| 2 | httplib 升级 + 测试 httplib::Client 多线程语义 | 2-3 天 |
| 3 | 验证现有测试 (Phase B/E/G 真并发) | 1 天 |
| 4 | 移除 SerializingDecorator 默认包装 (OPT-IN) | 0.5 天 |
| 5 | 性能 benchmark (4 worker 并发 vs 串行 baseline) | 0.5 天 |
| **Total** | | **5-7 天** |

### 不在 scope

- ❌ 不立即移除 SerializingDecorator (Wave 1 #2 ship 后 SerializingDecorator
  保留默认行为, 直到本 change 验证完成)
- ❌ 不修改 httplib/OpenSSL 升级路径外的其他依赖 (Taskflow / nlohmann_json /
  yaml-cpp 等待升级触发独立 change)
- ❌ 不重写 CloudLLMAdapter 协议层 (OpenAI 兼容协议契约保持)

## Scope Boundaries (In)

- ✅ 创建 ADR `Cloud adapter threading model` (替代占位符 `ADR-XXXX`)
- ✅ 记录根因诊断细节 (OpenSSL SSL_CTX 竞态 + httplib Authorization 栈 bug)
- ✅ 记录 SerializingDecorator fail-safe 设计 (OPT-IN 后保留)
- ✅ 实施时升级路径 + 验证清单

## Scope Boundaries (Out)

- ❌ 当前 change 不实施 OpenSSL/httplib 升级 (估时 5-7 天)
- ❌ 当前 change 不重写 CloudLLMAdapter 协议层
- ❌ 当前 change 不迁移其他 decorator (CostTracking/Compliance/RateLimit)

## Impact

**追踪物价值**:
- 防止 SerializingDecorator N→1 性能税意外固化
- 为后续 Phase E/G 多 worker 真实 LLM 提供根因路径
- 集中 OpenSSL/httplib 升级相关风险评估

**当前影响**: 零 (仅追踪物)

## 升级触发 (Escalation)

若以下任一条件满足, 本 change 应**立即升级到 P0 实施**:
- Phase E/G 启用真实 LLM 后出现新的多线程 SIGSEGV 变种
- OpenSSL 发布 4.0 (升级收益更显著)
- httplib 发布明确的多线程 fix (升级路径清晰)
- 项目其他模块 (Skill IPC / pdk_chat_demo) 出现多线程 LLM 问题

## 验证标准

- ADR 文件创建: `docs/adr/adr-XXXX-cloud-adapter-threading-model.md`
  (替换占位符 ADR-XXXX → 实际 ADR 号)
- ADR 包含: 决策 / 根因诊断 / 升级路径 / 不变量风险 / 验证清单
- 现有 4 个文件 (serializing_decorator.h/.cpp + tests) 中 `ADR-XXXX` 引用
  更新为真实 ADR 号
- `openspec validate cloud-adapter-threading-root-cause --strict` exit 0

## 估时

| 阶段 | 内容 | 估时 |
|---|---|---|
| 1 | ADR 创建 + 引用更新 | 30 min |
| 2 | 升级路径记录 | 30 min |
| 3 | 验证 + archive | 15 min |
| **Total** | | **~1.25 h (追踪物)** |
| **实施 (后续 change)** | OpenSSL + httplib 升级 | 5-7 天 |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| 本 change 仅追踪, 实际升级永远不做 | 升级触发条件明确 (本 change §升级触发); 每次 Sprint 收官 review 评估触发条件 |
| OpenSSL 升级破坏 ABI/API 兼容 | 升级前 review OpenSSL migration guide; Phase 5 兼容性测试 |
| httplib upstream 多线程 fix 未到位 | 保留 SerializingDecorator 作为 OPT-IN fail-safe |