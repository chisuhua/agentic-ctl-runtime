## Phase 1 — ADR 创建 + 引用更新 (追踪物)

- [ ] 1.1 创建 `docs/adr/adr-XXXX-cloud-adapter-threading-model.md`
      (替换占位符 ADR-XXXX → 实际分配 ADR 号, 预计 ADR-0076)
      — 包含: 决策 / 根因诊断 / 升级路径 / 不变量风险 / 验证清单
- [ ] 1.2 更新 `src/common/llm/serializing_decorator.h` L10/L47
      `ADR-XXXX (Cloud adapter threading model)` → 实际 ADR 号
- [ ] 1.3 更新 `src/common/llm/serializing_decorator.cpp` 注释
- [ ] 1.4 更新 `tests/test_serializing_decorator.cpp` 注释
- [ ] 1.5 更新 `tests/test_domain_worker_pool.cpp` KNOWN ISSUE 注释
      (如适用, 删除因 Wave 1 #2 已 ship)
- [ ] 1.6 更新 `openspec/changes/fix-cloud-adapter-multithreading/proposal.md`
      "follow-up: ADR-XXXX" 引用

## Phase 2 — 升级路径记录 (本 change 主体)

- [ ] 2.1 ADR 内记录 OpenSSL 3.0 集成步骤 (find_package + 链接)
- [ ] 2.2 ADR 内记录 httplib upstream 多线程 fix 调研 checklist
- [ ] 2.3 ADR 内记录 SerializingDecorator fail-safe OPT-IN 设计
- [ ] 2.4 ADR 内记录实施触发条件 (Phase E/G 多 worker 真实 LLM 启用等)
- [ ] 2.5 ADR 内记录性能 trade-off baseline (4 worker 串行 vs 并行)

## Phase 3 — 验证 + archive

- [ ] 3.1 ADR 通过 `tools/adr_lint.py` 0 errors
- [ ] 3.2 `openspec validate cloud-adapter-threading-root-cause --strict` exit 0
- [ ] 3.3 grep `ADR-XXXX` 全代码库应仅出现在本 change 文档中 (其他位置已更新)
- [ ] 3.4 `tools/docs_drift_audit.py` 0 CRITICAL drift
- [ ] 3.5 commit (本 change archive 前)
- [ ] 3.6 `openspec archive cloud-adapter-threading-root-cause -y`

## Tasks 总数

| Phase | Sub-tasks | 产出 |
|---|---|---|
| 1 (ADR + 引用) | 6 | 1 ADR + 5 文件注释更新 |
| 2 (路径记录) | 5 | ADR 内部 5 章节 |
| 3 (验证 + archive) | 6 | ship gate |
| **Total** | **17 sub-tasks** | **1 ADR + 5 注释更新 + archive** |

## 估时

| 阶段 | 估时 |
|---|---|
| 1 | 30 min |
| 2 | 30 min |
| 3 | 15 min |
| **Total** | **~1.25 h** |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| 占位符 ADR-XXXX 已存在多处, 漏更新导致引用悬空 | Phase 1.3-1.6 显式列出 5 个位置, 完成后 grep 验证 |
| ADR 编号分配冲突 (实际已有 ADR-0075) | 创建前查 `docs/adr/` 现有最大编号 +1 |
| 本 change 仅追踪, 实际升级永远不做 | 升级触发条件在 proposal.md §升级触发明确; 每次 Sprint 收官 review |
| OpenSSL 3.0 ABI 兼容性 | ADR 记录 BREAKING; 实施时先评估下游影响 |