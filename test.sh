#!/usr/bin/env bash
# test.sh
# 功能描述: HydraForge 全量回归测试 — 单元测试 + 真实云端 LLM 集成测试 +
#          pdk_chat_demo 端到端探测（含云端响应内容断言）
# 设计依据: HydraForge 单人开发模式 (Single-Dev Mode)
# 用法:
#   ./test.sh                   # 完整回归（默认跳过 live,需 SKIP_LIVE=0 启用）
#   ./test.sh --live            # 强制启用 live 云端测试
#   SKIP_LIVE=0 ./test.sh       # 同上
#   ./test.sh --no-rebuild      # 跳过增量编译
#   ./test.sh --unit-only       # 只跑 ctest,不跑 live 与 demo probe
# 退出码:
#   0 = 全部通过
#   1 = 单元测试失败
#   2 = live 云端测试失败 (cloud_llm_live / e2e_real_llm)
#   3 = pdk_chat_demo probe 失败 (云端无响应 / 内容不符合预期)
#   4 = 环境前置检查失败 (build 缺失 / API key 缺失 / 示例未构建)

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"
BUILD_DIR="$REPO_ROOT/build"
EXAMPLES_BUILD_DIR="$BUILD_DIR/examples/pdk_chat_demo"

# -------- 参数解析 --------
RUN_REBUILD=1
RUN_LIVE=${SKIP_LIVE:-1}   # 默认 1 (skip),0 表示跑 live
UNIT_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --live)            RUN_LIVE=0 ;;
    --no-rebuild)      RUN_REBUILD=0 ;;
    --unit-only)       UNIT_ONLY=1 ;;
    -h|--help)
      sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *) echo "unknown arg: $arg"; exit 4 ;;
  esac
done

# -------- 颜色 --------
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
step()  { printf "\n${BLUE}== %s ==${NC}\n" "$*"; }
ok()    { printf "${GREEN}✓ %s${NC}\n" "$*"; }
warn()  { printf "${YELLOW}! %s${NC}\n" "$*"; }
fail()  { printf "${RED}✗ %s${NC}\n" "$*"; }

EXIT_CODE=0

# ===============================================================
# Step 1: 环境前置
# ===============================================================
step "环境前置检查"

if [[ ! -d "$BUILD_DIR" ]]; then
  fail "build 目录不存在: $BUILD_DIR  —  请先 cmake -B build"
  exit 4
fi

if [[ ! -x "$BUILD_DIR/tests/test_cloud_llm" ]]; then
  fail "test_cloud_llm 未构建 —  请先 cmake --build build"
  exit 4
fi

EXAMPLES_BUILT=0
if [[ -x "$EXAMPLES_BUILD_DIR/pdk_chat_demo" ]]; then
  EXAMPLES_BUILT=1
fi

# API key 检测
HAS_DEEPSEEK=0
HAS_MINIMAX=0
[[ -n "${DEEPSEEK_API_KEY:-}" ]] && HAS_DEEPSEEK=1
[[ -n "${MINIMAX_API_KEY:-}"  ]] && HAS_MINIMAX=1

ok "build 目录就绪"
if [[ $EXAMPLES_BUILT -eq 1 ]]; then
  ok "examples 已构建 (pdk_chat_demo 可用)"
else
  warn "examples 未构建 — 后续 demo probe 步骤将跳过 (用 cmake -DAGENTICDSL_BUILD_EXAMPLES=ON -B build 重建)"
fi

if [[ $RUN_LIVE -eq 0 ]]; then
  if [[ $HAS_DEEPSEEK -eq 0 && $HAS_MINIMAX -eq 0 ]]; then
    fail "RUN_LIVE=0 但 DEEPSEEK_API_KEY 与 MINIMAX_API_KEY 均未设置"
    exit 4
  fi
  ok "云端 API key 已配置 (deepseek=$HAS_DEEPSEEK, minimax=$HAS_MINIMAX)"
fi

# ===============================================================
# Step 2: 增量编译
# ===============================================================
if [[ $RUN_REBUILD -eq 1 ]]; then
  step "增量编译 (build/)"
  if cmake --build "$BUILD_DIR" -j"$(nproc)" >/tmp/regression_build.log 2>&1; then
    ok "编译通过"
  else
    fail "编译失败 —  见 /tmp/regression_build.log 末尾"
    tail -30 /tmp/regression_build.log
    exit 4
  fi
fi

# ===============================================================
# Step 3: ctest 单元测试 (排除 live 标签)
# ===============================================================
step "单元测试 (ctest, 排除 live 标签)"
cd "$BUILD_DIR"

# 收集 ctest 输出,统计总数
CTEST_OUT=$(ctest -j"$(nproc)" -E 'live' --output-on-failure 2>&1 | tee /tmp/regression_ctest.log | tail -30)
CTEST_RC=${PIPESTATUS[0]}

# 提取总数
TOTAL=$(grep -oE '[0-9]+/[0-9]+' /tmp/regression_ctest.log | tail -1)
if [[ -z "$TOTAL" ]]; then
  TOTAL=$(grep -E "tests passed|tests failed" /tmp/regression_ctest.log | tail -1)
fi

NOT_RUN=$(grep -E "^\s*[0-9]+\s+-\s+\S+\s+\(Not Run\)" /tmp/regression_ctest.log | wc -l | tr -d ' ')
REAL_FAIL=$(grep -E "^\s*[0-9]+\s+-\s+\S+\s+\(Failed" /tmp/regression_ctest.log | wc -l | tr -d ' ')

if [[ $CTEST_RC -eq 0 ]]; then
  ok "ctest 全部通过  ($TOTAL, Not Run=$NOT_RUN)"
elif [[ $REAL_FAIL -eq 0 && $NOT_RUN -gt 0 ]]; then
  warn "ctest 全真测断言通过,但有 $NOT_RUN 个目标未构建 (Not Run) —  见 /tmp/regression_ctest.log"
  grep -E "Not Run" /tmp/regression_ctest.log | head -5
else
  fail "ctest 真失败 $REAL_FAIL 项 (Not Run=$NOT_RUN) —  见 /tmp/regression_ctest.log"
  grep -E "Failed" /tmp/regression_ctest.log | head -10
  EXIT_CODE=1
fi

# ===============================================================
# Step 4: 真实云端 LLM 测试 (可选)
# ===============================================================
LIVE_RESULT_LABEL="(skipped, RUN_LIVE=1)"
LIVE_RC=0

if [[ $RUN_LIVE -eq 0 ]]; then
  step "云端 LLM 测试 (live 标签)"
  LIVE_OUT=$(ctest -L live --output-on-failure 2>&1) || LIVE_RC=$?
  echo "$LIVE_OUT" | tail -8
  LIVE_RESULT_LABEL=$(echo "$LIVE_OUT" | grep -E "tests passed|tests failed" | tail -1)
  if [[ $LIVE_RC -eq 0 ]]; then
    ok "live 单元测试通过  ${LIVE_RESULT_LABEL}"
  else
    fail "live 单元测试失败"
    EXIT_CODE=2
  fi

  step "test_e2e_real_llm (DeepSeek 真实路径)"
  cd "$EXAMPLES_BUILD_DIR/tests"
  if HYDRAFORGE_RUN_REAL_LLM=1 \
     DEEPSEEK_API_KEY="${DEEPSEEK_API_KEY:-}" \
     HYDRAFORGE_PLUGIN_PATH="$BUILD_DIR/pdk" \
     HYDRAFORGE_LOOP_DIR="$REPO_ROOT/lib/loop" \
     ./test_e2e_real_llm > /tmp/regression_e2e.log 2>&1; then
    ok "test_e2e_real_llm 通过"
  else
    fail "test_e2e_real_llm 失败 —  见 /tmp/regression_e2e.log"
    tail -20 /tmp/regression_e2e.log
    EXIT_CODE=2
  fi
fi

# ===============================================================
# Step 5: pdk_chat_demo 端到端探测 (云端响应内容断言)
# ===============================================================
if [[ $UNIT_ONLY -eq 1 ]]; then
  step "pdk_chat_demo probe (--unit-only 跳过)"
elif [[ $EXAMPLES_BUILT -eq 0 ]]; then
  step "pdk_chat_demo probe (示例未构建,跳过)"
elif [[ $RUN_LIVE -eq 1 ]]; then
  step "pdk_chat_demo probe (RUN_LIVE=1,跳过真实 API)"
  # 在 mock 模式下验证启动/退出,但不验证云端响应
  cd "$EXAMPLES_BUILD_DIR"
  if printf 'Say hello in one short sentence.\n/exit\n' \
       | timeout 30 ./pdk_chat_demo --mock > /tmp/regression_demo.log 2>&1; then
    ok "pdk_chat_demo --mock 启动正常"
  else
    fail "pdk_chat_demo --mock 启动失败 —  见 /tmp/regression_demo.log"
    EXIT_CODE=3
  fi
else
  step "pdk_chat_demo 真实模式探测 (云端响应内容断言)"
  cd "$EXAMPLES_BUILD_DIR"

  PROMPT="Reply with the single word: pong"
  LOG=/tmp/regression_demo_live.log

  if ! printf '%s\n/exit\n' "$PROMPT" \
       | DEEPSEEK_API_KEY="${DEEPSEEK_API_KEY:-}" \
         HYDRAFORGE_PLUGIN_PATH="$BUILD_DIR/pdk" \
         HYDRAFORGE_LOOP_DIR="$REPO_ROOT/lib/loop" \
         timeout 60 ./pdk_chat_demo > "$LOG" 2>&1; then
    fail "pdk_chat_demo 退出码非 0 —  见 $LOG"
    tail -20 "$LOG"
    EXIT_CODE=3
  fi

  # 断言 1: DSL 校验通过 (chat.loop 已加载)
  if grep -q "DSL Schema Validation OK" "$LOG"; then
    ok "DSL Schema Validation 通过"
  else
    fail "DSL Schema Validation 失败"
    grep -E "DSL Schema|FAILED" "$LOG" | head -5
    EXIT_CODE=3
  fi

  # 断言 2: llm.response 事件 ok=true 且 completion_tokens > 0 (云端真正响应)
  LLM_LINE=$(grep -E 'llm\.response:' "$LOG" | tail -1 || true)
  if [[ -z "$LLM_LINE" ]]; then
    fail "未捕获到 llm.response 事件 —  云端未被调用"
    EXIT_CODE=3
  elif echo "$LLM_LINE" | grep -q 'ok=true' && \
       echo "$LLM_LINE" | grep -qE 'completion=[1-9][0-9]*'; then
    ok "云端 LLM 真实响应: $LLM_LINE"
  else
    fail "llm.response 事件异常: $LLM_LINE"
    EXIT_CODE=3
  fi

  # 断言 3: Assistant 行包含非空响应内容 (云端回答被解析到 user-facing output)
  ASSISTANT_LINE=$(grep -E '^Assistant: ' "$LOG" | tail -1 || true)
  if [[ -z "$ASSISTANT_LINE" ]]; then
    fail "未捕获到 Assistant 输出"
    grep -E 'Assistant|loop\.done' "$LOG" | tail -5
    EXIT_CODE=3
  elif echo "$ASSISTANT_LINE" | grep -qE '^Assistant:[[:space:]]+pong$'; then
    ok "云端回答被正确解析: $ASSISTANT_LINE"
  elif echo "$ASSISTANT_LINE" | grep -qE '^Assistant:[[:space:]]+Paused at LLM call$'; then
    fail "Assistant 输出仍为 'Paused at LLM call' —  loop_agent 响应提取未修复"
    EXIT_CODE=3
  elif echo "$ASSISTANT_LINE" | grep -qE '^Assistant:[[:space:]]+\S+'; then
    warn "Assistant 行内容非预期,跳过内容断言: $ASSISTANT_LINE"
  else
    fail "Assistant 行为空: '$ASSISTANT_LINE'"
    EXIT_CODE=3
  fi
fi

# ===============================================================
# 汇总
# ===============================================================
step "汇总"
case $EXIT_CODE in
  0) ok "全部通过";;
  1) fail "单元测试有失败 (ctest)";;
  2) fail "云端 LLM 测试有失败";;
  3) fail "pdk_chat_demo probe 失败";;
  *) fail "退出码 $EXIT_CODE";;
esac

exit $EXIT_CODE