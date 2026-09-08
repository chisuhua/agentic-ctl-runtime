#!/usr/bin/env bash
# scripts/check-model-default-cleared.sh
# 功能描述: 静态契约守卫 — 验证 5 个 LLMParams 潜伏站点都调用了 params.model.clear()
#          防未来维护者误删修复 (AGENTS.md 模式 #1 test-driven bug discovery closed loop
#          step #2 要求: clear() 看似冗余但绝非, 必须加注释 + 静态脚本兜底)
# 设计依据: openspec/changes/fix-generation-request-model-default/tasks.md §2.6
# 用法:
#   ./scripts/check-model-default-cleared.sh                # 全部 5 站点必须找到
#   ./scripts/check-model-default-cleared.sh --verbose       # 详细输出
#   ./scripts/check-model-default-cleared.sh --strict-exit   # 找到 → exit 0, 漏 → exit 1
# 退出码:
#   0 = 全部 5 站点找到 params.model.clear() (默认)
#   1 = 漏站点或脚本本身错误 (--strict-exit)
#
# 维护原则: 每新增 GenerationRequest 构造站点须同步追加到此脚本 (5 → 6 → ...)

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

VERBOSE=0
STRICT_EXIT=0
for arg in "$@"; do
  case "$arg" in
    --verbose|-v) VERBOSE=1 ;;
    --strict-exit) STRICT_EXIT=1 ;;
    --help|-h)
      sed -n '2,30p' "$0"
      exit 0
      ;;
    *) echo "Unknown arg: $arg" >&2; exit 2 ;;
  esac
done

# 5 个 LLMParams 潜伏站点 (oracle ses_f7f5ef175ffeGKhxXLfBJjzLVX 实证)
# 格式: "file|search_pattern|description"
SITES=(
  "src/modules/executor/node_executor.cpp|req.params.model.clear()|GenerateSubgraphNode ll_call (was line 356)"
  "src/modules/executor/node_executor.cpp|req.params.model.clear()|YieldNode generate_stream (was line 574, same file as above)"
  "src/modules/skill_interpreter/skill_interpreter.cpp|gen_req.params.model.clear()|IPC llm_generate (was line 657-659)"
  "src/core/context_compactor.cpp|req.params.model.clear()|ContextCompactor::compact summary (was line 60)"
  "src/modules/cognitive/gepa_loop.cpp|request.params.model.clear()|GEPA reflection (was line 115)"
)

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

missing=0
total=${#SITES[@]}

echo "=== Static contract check: params.model.clear() in 5 LLM shadow sites ==="
echo "Source: openspec/changes/fix-generation-request-model-default/tasks.md §2.6"
echo

for i in "${!SITES[@]}"; do
  IFS='|' read -r file pattern desc <<< "${SITES[$i]}"
  site_num=$((i + 1))
  if [[ ! -f "$file" ]]; then
    echo -e "${RED}[$site_num/$total] MISSING FILE: $file${NC}"
    missing=$((missing + 1))
    continue
  fi
  count=$(grep -c "$pattern" "$file" 2>/dev/null || echo "0")
  if [[ "$count" -ge 1 ]]; then
    line_no=$(grep -n "$pattern" "$file" | head -1 | cut -d: -f1)
    if [[ "$VERBOSE" == "1" ]]; then
      echo -e "${GREEN}[$site_num/$total] OK${NC} ($line_no) $file — $desc"
    else
      echo -e "${GREEN}[$site_num/$total] OK${NC} $file:$line_no — $desc"
    fi
  else
    echo -e "${RED}[$site_num/$total] MISSING${NC} $file — $desc"
    echo "  expected pattern: $pattern"
    missing=$((missing + 1))
  fi
done

echo
echo "=== Summary ==="
if [[ "$missing" -eq 0 ]]; then
  echo -e "${GREEN}All $total sites have params.model.clear() (fix regression guard intact)${NC}"
  exit 0
else
  echo -e "${RED}$missing of $total sites MISSING params.model.clear()${NC}"
  echo "Action: Re-apply fix per openspec/changes/fix-generation-request-model-default/"
  if [[ "$STRICT_EXIT" == "1" ]]; then
    exit 1
  fi
  exit 1
fi