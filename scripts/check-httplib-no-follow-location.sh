#!/usr/bin/env bash
# check-httplib-no-follow-location.sh
# 文件头注释
# 功能描述：CVE-2026-33745 守卫 — 禁止项目代码调用 set_follow_location(true)
#          (Authorization + follow_location + cross-origin redirect → 凭证泄露给第三方)
# 设计依据：openspec/changes/upgrade-httplib-0541/{proposal,design}.md D5
# 作者：AgenticDSL Sprint 25 (Oracle L2 评审)
# 最后修改日期：2026-09-10
#
# 用法：./scripts/check-httplib-no-follow-location.sh
# 返回：exit 0 = 通过 (全项目 0 使用), exit 1 = 失败 (发现调用)
set -euo pipefail

cd "$(dirname "$0")/.."

# 排除 external/ (vendored httplib.h 自身含 set_follow_location API 声明, 非使用)
# 排除 docs/ (审计报告中会提及 set_follow_location 字样, 非使用)
MATCHES=$(grep -rn "set_follow_location" src/ pdk/ examples/ tests/ 2>/dev/null || true)

if [ -n "$MATCHES" ]; then
  echo "ERROR: set_follow_location 被项目代码使用 (CVE-2026-33745 凭证泄露风险):"
  echo ""
  echo "$MATCHES"
  echo ""
  echo "修复方法："
  echo "  1. 若需 redirect 跟随, 改用手动实现并自行处理 Authorization header"
  echo "  2. 若无需 redirect, 删除该调用"
  echo "  3. 在 ADR-0087 §实施日志说明为何当前升级后 httplib v0.54.1 已覆盖 CVE"
  exit 1
fi

echo "OK: set_follow_location 全项目 0 使用 (排除 external/ + docs/)"
echo "CVE-2026-33745 守卫通过"
exit 0
