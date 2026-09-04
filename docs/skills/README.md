# SKILL 目录

> **命令式 SKILL（`.skill.md`）** vs **LLM Prompt Template（`.md`）**

## 区别

| 特征 | `.skill.md` | `.md` |
|------|-------------|-------|
| 内容 | 命令式 DSL 语句（`call_tool` / `return`；v0.3 起仅支持顶层 call_tool） | 自然语言提示词模板 |
| 执行器 | `SkillInterpreter::run()` (隔离进程 + seccomp) | LLM provider 注入 |
| 隔离 | ✅ 进程级隔离 (posix_spawn + seccomp BPF) | ❌ 与主进程同地址空间 |
| 速度 | 微秒级 IPC | LLM 推理延迟 |
| 用例 | 确定性工具编排 | 非确定性推理生成 |

## 文件列表

| 文件 | 形态 | 说明 |
|------|------|------|
| `code-review-run.skill.md` | 命令式 (v0.3) | `fs.read` + `code_review/run` 工具编排示例；`return <tool_result_var>` 模式 |
| `../../skills/code-review/SKILL.md` | Prompt Template | LLM 提示词模板（mock-only，需 LLM 注入） |

## 创建新 SKILL

1. 确定形态：确定性工具流程 → 命令式；非确定性生成 → Prompt Template
2. 创建 `.skill.md` 文件（命令式）或 `.md` 文件（Prompt Template）
3. 在 `config.json` 中添加条目
4. 重启 demo 验证

## 命令式 SKILL v0.3 语法（SkillInterpreter 实际支持范围）

- 仅支持**顶层** `call_tool("name", {args})` 语句（每行一条）；`assign x = call_tool(...)` RHS 形式 V1 不支持（RHS 视作字面字符串）
- 工具结果存入 `vars["<tool_name>"]`，其中 `/` → `_`，`.` 保持不变（如 `code_review/run` → `code_review_run`，`fs.read` → `fs.read`）
- `return <expr>` 只能引用已存入的变量名（vars 中存在该 key）；不可引用未定义变量（inja 渲染未定义变量抛异常 → 子进程 SIGABRT）
- args 模板插值仅支持已 `assign` 的变量；不要在 args JSON 中插入未定义的 `{{var}}`

## C++ 集成入口（U2 实现）

`code-review-run.skill.md` v0.3 配套 C++ 集成测试：

```bash
# 构建
cmake -S . -B build-u2 -DAGENTICDSL_BUILD_EXAMPLES=ON -DAGENTICDSL_BUILD_TESTS=ON
cmake --build build-u2 --target test_code_review_skill -j$(nproc)

# 运行（4 类用例：成功路径 / 缺失文件 / 未授权工具拒绝 / 非 Linux 降级）
ctest --test-dir build-u2 -R test_code_review_skill --output-on-failure
```

测试通过 `SkillInterpreter::run()` 在隔离子进程加载 skill，使用 `default_skill_capability()`（工具白名单 `fs.read` + `code_review/run`），mock-only 验证编排正确性与 capability fail-closed 行为。测试二进制使用 `main_skill_test_runner.cpp` 自定义 main（支持 `--skill-child` 分发），由 `examples/pdk_chat_demo/tests/CMakeLists.txt` 注册。

详细语法见 [skill-dsl-syntax.md](skill-dsl-syntax.md)。