# cloud-adapter-threading-root-cause

**Status**: Draft (scaffold, Oracle ship-gate P2 fix item)

## Scope

追踪 `fix-cloud-adapter-multithreading` (Wave 1 #2) SerializingDecorator 的
**根因修复路径** — 创建 ADR 追踪物, 防止 N→1 性能税意外固化为永久架构.

## Why

SerializingDecorator (mutex + cv 串行化) 是**规避**, 不是根因修复:
- 根因: OpenSSL `SSL_CTX` 多线程初始化竞态 + httplib Authorization header 栈 bug
- 代价: N worker → 1 LLM call at a time (性能税)
- 风险: 性能税永久固化 (无追踪物), httplib/OpenSSL 漏洞在其他场景复现

## 验证

- ADR 文件创建并通过 lint
- 所有 `ADR-XXXX` 占位符引用更新为真实 ADR 号
- `openspec validate cloud-adapter-threading-root-cause --strict` exit 0
- `tools/adr_lint.py` 0 errors
- `openspec archive cloud-adapter-threading-root-cause -y`

## 依赖

- **被依赖**: 后续实际升级 changes (OpenSSL 3.0 集成 / httplib 升级 / 移除默认包装)
- **触发条件** (升级到 P0):
  - Phase E/G 启用真实 LLM 后出现新的多线程 SIGSEGV 变种
  - OpenSSL 发布 4.0
  - httplib 发布明确的多线程 fix
  - 其他模块 (Skill IPC / pdk_chat_demo) 出现多线程 LLM 问题

## Artifacts

- `proposal.md` — Why / What / Scope / 升级路径 / 触发条件 / 估时
- `design.md` — 根因诊断 / 升级路径 5 步骤 / 兼容性 / 实施顺序
- `tasks.md` — 17 sub-tasks across 3 phases
- `specs/cloud-adapter-threading-root-cause/spec.md` — 4 ADDED Requirements

## 实施升级时 (后续 change 估时 5-7 天)

1. OpenSSL 3.0 集成 (1-2 天)
2. httplib 升级 + 测试 (2-3 天)
3. 现有测试验证 (1 天)
4. 移除 SerializingDecorator 默认包装 (0.5 天)
5. 性能 benchmark (0.5 天)

每次 Sprint 收官 review 升级触发条件, 满足条件时升 P0.