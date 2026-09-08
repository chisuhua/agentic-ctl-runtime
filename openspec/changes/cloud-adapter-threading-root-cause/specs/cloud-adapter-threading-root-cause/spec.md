# Spec: cloud-adapter-threading-root-cause

## Purpose

追踪 `fix-cloud-adapter-multithreading` (Wave 1 #2) 的 SerializingDecorator
**根因修复路径**, 防止 N→1 性能税意外固化为永久架构. 当前 change 创建 ADR 追踪物,
不实施实际升级 (估时 5-7 天独立估时).

## ADDED Requirements

### Requirement: ADR SHALL be created to replace ADR-XXXX placeholder

本 change SHALL 创建 `docs/adr/adr-NNNN-cloud-adapter-threading-model.md`
(NNNN = 实际分配 ADR 号, 预计 ADR-0076), 替代现有 `ADR-XXXX` 占位符.
ADR SHALL 包含以下章节: 决策 / 根因诊断 / 升级路径 / 不变量风险 / 验证清单.

#### Scenario: ADR 文件创建并通过 lint
- GIVEN 当前 `ADR-XXXX (Cloud adapter threading model)` 是占位符
- WHEN 创建 ADR 文件
- THEN `docs/adr/adr-XXXX-cloud-adapter-threading-model.md` (实际编号替换) 存在
- AND `tools/adr_lint.py` exit 0
- AND ADR 含 5 章节 (决策/根因/升级/风险/验证)

### Requirement: existing code references SHALL be updated to real ADR number

本 change SHALL 更新所有 `ADR-XXXX (Cloud adapter threading model)` 占位符引用
为实际 ADR 号. 引用位置清单:
- `src/common/llm/serializing_decorator.h` (header doc, 2 处)
- `src/common/llm/serializing_decorator.cpp` (implementation comments, 1 处)
- `tests/test_serializing_decorator.cpp` (test comments, 1 处)
- `tests/test_cloud_adapter_multithread.cpp` (test comments, 1 处)
- `openspec/changes/fix-cloud-adapter-multithreading/proposal.md` (follow-up note, 1 处)
- `openspec/changes/fix-cloud-adapter-multithreading/design.md` (follow-up note, 1 处)

#### Scenario: grep ADR-XXXX 仅在本 change 文档中出现
- GIVEN ADR 实际编号已分配
- WHEN `grep -rn "ADR-XXXX" src/ tests/ docs/` 扫整个代码库
- THEN 仅在 `openspec/changes/cloud-adapter-threading-root-cause/` 内出现
- AND 其他位置已替换为真实 ADR 号

### Requirement: ADR SHALL document OpenSSL 3.0 + httplib upgrade path

ADR SHALL 记录真根因修复路径: OpenSSL 1.1+ → 3.0 升级 + httplib upstream
多线程 fix 调研 + 升级后验证清单 (Phase B B.2 / Phase E Skill IPC /
Phase G ContextCompactor 多 worker 真并发).

#### Scenario: ADR §升级路径 含 5 步骤
- GIVEN ADR 实际编号已分配
- WHEN 读取 ADR §升级路径
- THEN 含 5 步骤: OpenSSL 3.0 集成 / httplib 升级 / 验证 / 移除默认包装 / benchmark
- AND 每步骤含估时与风险记录

### Requirement: SerializingDecorator SHALL remain as OPT-IN fail-safe

升级路径 SHALL 保留 SerializingDecorator 作为 OPT-IN fail-safe, 不删除.
`LLMProviderFactory::create(config, opts)` opts.serializer = true 时启用,
默认 false (升级后无需串行化).

#### Scenario: 升级后 SerializingDecorator 仍可用
- GIVEN OpenSSL 3.0 + httplib 升级已完成 (后续独立 change)
- WHEN 默认 factory.create() 调用
- THEN 返回的 provider 是 CloudLLMAdapter 直接包装 (无 SerializingDecorator)
- AND 显式 opts.serializer = true 时返回 SerializingDecorator 包装
- AND SerializingDecorator 类 + 文件保留在代码库

### Requirement: scope 边界 (Out of Scope)

本 change SHALL NOT 实施 OpenSSL/httplib 实际升级 (估时 5-7 天, 后续独立 change);
下列 SHALL 明确排除在 scope 外.

#### Scenario: OpenSSL 升级 SHALL NOT be in this change
- 归属: 后续独立 change (估时 1-2 天)
- 理由: 本 change 仅追踪物, 实际升级独立估时

#### Scenario: httplib 升级 SHALL NOT be in this change
- 归属: 后续独立 change (估时 2-3 天)
- 理由: 上游版本调研 + 集成测试独立估时

#### Scenario: SerializingDecorator 类删除 SHALL NOT be in this change
- 归属: 后续升级完成后的独立 change
- 理由: 默认行为修改独立评估 (升级验证后才移除默认包装)

## 验证标准

- ADR 文件创建并通过 `tools/adr_lint.py`
- 所有 `ADR-XXXX` 占位符引用更新为真实 ADR 号
- `grep -rn "ADR-XXXX" src/ tests/ docs/` 仅在本 change 文档中出现
- `openspec validate cloud-adapter-threading-root-cause --strict` exit 0
- `tools/adr_lint.py` 0 errors
- `tools/docs_drift_audit.py` 0 CRITICAL drift
- `openspec archive cloud-adapter-threading-root-cause -y`

## References

- **根因诊断**: `openspec/changes/fix-cloud-adapter-multithreading/proposal.md` §根因候选
- **gdb backtrace**: `real-llm-core-coverage` Phase B ship commit `c0cb522`
- **SerializingDecorator 规避实现**: `src/common/llm/serializing_decorator.{h,cpp}`
- **Oracle ship-gate review**: session `ses_f7de8daa0ffeMA9GroBvS4MZOR` (P2 fix)
- **现有 ADR 编号**: `docs/adr/` 最大编号 (分配前查)