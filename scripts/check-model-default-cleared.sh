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
# 格式: "file|search_pattern|min_count|anchor_pattern|description"
#   - search_pattern: 主匹配 (params.model.clear())
#   - min_count: 该文件内至少出现次数 (防双站点退化: node_executor.cpp 有 2 处 clear)
#   - anchor_pattern: 站点独有上下文标识 (二次确认, 防同 pattern 错配)
SITES=(
  "src/modules/executor/node_executor.cpp|req.params.model.clear()|2|GenerateSubgraphNode|GenerateSubgraphNode ll_call (was line 356) + YieldNode (was line 574, 同文件 2 处)"
  "src/modules/executor/node_executor.cpp|req.params.model.clear()|2|fix-yield-node-token-passthrough|YieldNode generate_stream 锚点 (二次确认 YieldNode 注释存在, 防 GenerateSubgraphNode 单 clear 误删 YieldNode clear 不被拦截)"
  "src/modules/skill_interpreter/skill_interpreter.cpp|gen_req.params.model.clear()|1||IPC llm_generate (was line 657-659)"
  "src/core/context_compactor.cpp|req.params.model.clear()|1||ContextCompactor::compact summary (was line 60)"
  "src/modules/cognitive/gepa_loop.cpp|request.params.model.clear()|1||GEPA reflection (was line 115)"
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
  IFS='|' read -r file pattern min_count anchor desc <<< "${SITES[$i]}"
  site_num=$((i + 1))
  if [[ ! -f "$file" ]]; then
    echo -e "${RED}[$site_num/$total] MISSING FILE: $file${NC}"
    missing=$((missing + 1))
    continue
  fi
  count=$(grep -c "$pattern" "$file" 2>/dev/null || echo "0")
  if [[ "$count" -ge "$min_count" ]]; then
    # anchor 二次确认 (空 anchor 跳过)
    if [[ -n "$anchor" ]]; then
      if ! grep -q "$anchor" "$file"; then
        echo -e "${RED}[$site_num/$total] MISSING ANCHOR${NC} $file — $desc"
        echo "  expected anchor: $anchor (sites in same file share pattern, anchor distinguishes)"
        missing=$((missing + 1))
        continue
      fi
    fi
    line_no=$(grep -n "$pattern" "$file" | head -1 | cut -d: -f1)
    if [[ "$VERBOSE" == "1" ]]; then
      echo -e "${GREEN}[$site_num/$total] OK${NC} ($line_no, count=$count, min=$min_count) $file — $desc"
    else
      echo -e "${GREEN}[$site_num/$total] OK${NC} $file:$line_no — $desc"
    fi
  else
    echo -e "${RED}[$site_num/$total] MISSING${NC} $file — $desc"
    echo "  expected pattern: $pattern (count >= $min_count, got $count)"
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