#!/usr/bin/env bash
# scripts/evidence-gate-v1.sh
# Phase 7a Evidence Gate decision script (per OpenSpec phase-7a-baseline-retest-v2)
# Decision 5: --write-markdown option for control-plane-eval.py C5 auto-consumption
# Decision 6: bash 重写决策逻辑 (不调用 C++ evaluate_gate, 两实现并存)
# Per Oracle B2: 完整 4 维判定表 (parse_valid + L1/L2/L3)
# Per Metis F1: parse_valid × 100 转 int 比较避免 jq 浮点尾数

set -euo pipefail

SCRIPT_NAME="$(basename "$0")"
BASELINE_JSON=""
WRITE_MARKDOWN=""

usage() {
    cat <<EOF
Usage: bash $SCRIPT_NAME <baseline.json> [--write-markdown <path>]

Evidence Gate v1 decision per ADR-0074 D5 v2 semantics:
  PASS:        parse_valid ≥90% AND L1 ≥70% AND L2 ≥50% AND L3 ≥30%
  Conditional: parse_valid ∈ [85%, 90%) AND L1 ≥70% (L2/L3 仅记录)
  FAIL:        parse_valid <85% OR L1 <70% OR (parse_valid ≥90% AND (L2 <50% OR L3 <30%))

Arguments:
  <baseline.json>              Path to baseline JSON (from measure-baseline.py)
  --write-markdown <path>      Optional: write decision markdown to <path>
                                (for control-plane-eval.py C5 auto-consumption)
EOF
}

# 参数解析
while [[ $# -gt 0 ]]; do
    case "$1" in
        --write-markdown)
            WRITE_MARKDOWN="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            if [[ -z "$BASELINE_JSON" ]]; then
                BASELINE_JSON="$1"
                shift
            else
                echo "ERROR: unknown argument: $1" >&2
                usage >&2
                exit 1
            fi
            ;;
    esac
done

if [[ -z "$BASELINE_JSON" ]]; then
    usage >&2
    exit 1
fi

if [[ ! -f "$BASELINE_JSON" ]]; then
    echo "ERROR: baseline JSON not found: $BASELINE_JSON" >&2
    exit 1
fi

# jq 可用性检查 (per Decision 2)
if ! command -v jq >/dev/null 2>&1; then
    echo "ERROR: jq not installed (per Decision 2 CI 依赖契约)" >&2
    exit 1
fi

# 读取数据 (per Metis F1: parse_valid × 100 转 int 比较)
read PARSE_VALID_INT L1 L2 L3 MOCK_MODE MODELS_PARTIAL <<<"$(jq -r '
    .summary.avg_parse_valid as $pv |
    .summary.avg_task_success.L1 as $l1 |
    .summary.avg_task_success.L2 as $l2 |
    .summary.avg_task_success.L3 as $l3 |
    .mock_mode as $mock |
    (.models_partial // []) as $mp |
    ($pv * 100 | floor | tostring) + " " +
    ($l1 * 100 | floor | tostring) + " " +
    ($l2 * 100 | floor | tostring) + " " +
    ($l3 * 100 | floor | tostring) + " " +
    ($mock | tostring) + " " +
    ($mp | join(","))
' "$BASELINE_JSON")"

BASELINE_ID="$(jq -r '.baseline_id // "unknown"' "$BASELINE_JSON")"

# mock_mode 标注 (per spec "Mock mode" scenario)
MOCK_NOTE=""
if [[ "$MOCK_MODE" == "true" ]]; then
    MOCK_NOTE=" (mock_baseline: true)"
fi

# 完整 4 维判定表 (per Oracle B2 修复, PARSE_VALID_INT/L1/L2/L3 均为 ×100 后的 int)
if [[ "$PARSE_VALID_INT" -lt 85 ]]; then
    VERDICT="FAIL"
    REASON="parse_valid <85%"
elif [[ "$L1" -lt 70 ]]; then
    VERDICT="FAIL"
    REASON="L1 <70%"
elif [[ "$PARSE_VALID_INT" -ge 90 ]]; then
    if [[ "$L2" -lt 50 ]]; then
        VERDICT="FAIL"
        REASON="parse_valid ≥90% but L2 <50%"
    elif [[ "$L3" -lt 30 ]]; then
        VERDICT="FAIL"
        REASON="parse_valid ≥90% but L3 <30%"
    else
        VERDICT="PASS"
        REASON="all 4 dimensions meet thresholds"
    fi
else
    # parse_valid ∈ [85%, 90%) AND L1 ≥70%
    VERDICT="Conditional"
    REASON="parse_valid in [85%, 90%)"
fi

# Partial Models 降级 (per spec "Partial Models 降级" scenario)
if [[ -n "$MODELS_PARTIAL" ]]; then
    if [[ "$VERDICT" != "FAIL" ]]; then
        ORIGINAL_VERDICT="$VERDICT"
        VERDICT="Conditional"
        REASON="partial models ($MODELS_PARTIAL); original=$ORIGINAL_VERDICT"
    fi
fi

# Mock mode 强制 Conditional (per spec "Mock mode" scenario + F1 修复)
# mock baseline 是占位符, MUST NOT 被当作真实 PASS (prevent control-plane-eval.py C5 误转 PASS)
if [[ "$MOCK_MODE" == "true" && "$VERDICT" == "PASS" ]]; then
    ORIGINAL_VERDICT="$VERDICT"
    VERDICT="Conditional"
    REASON="mock baseline placeholder; original=$ORIGINAL_VERDICT"
fi

# JSON 输出 (stdout)
VERDICT_JSON=$(jq -n \
    --arg verdict "$VERDICT" \
    --arg reason "$REASON" \
    --argjson parse_valid "$(awk "BEGIN { printf \"%.4f\", $PARSE_VALID_INT / 100 }")" \
    --argjson l1 "$(awk "BEGIN { printf \"%.4f\", $L1 / 100 }")" \
    --argjson l2 "$(awk "BEGIN { printf \"%.4f\", $L2 / 100 }")" \
    --argjson l3 "$(awk "BEGIN { printf \"%.4f\", $L3 / 100 }")" \
    --arg baseline_id "$BASELINE_ID" \
    --arg mock_note "$MOCK_NOTE" \
    --arg models_partial "$MODELS_PARTIAL" \
    '{verdict: $verdict, reason: $reason, parse_valid: $parse_valid, l1: $l1, l2: $l2, l3: $l3, baseline_id: $baseline_id} + (if $mock_note != "" then {mock_baseline: true} else {} end) + (if $models_partial != "" then {models_partial: ($models_partial | split(","))} else {} end)')

echo "$VERDICT_JSON" | jq '.'

# Markdown 输出 (stdout + 可选文件)
MARKDOWN="# Evidence Gate v1 Verdict

- **Baseline ID**: $BASELINE_ID
- **Verdict**: $VERDICT
- **Reason**: $REASON${MOCK_NOTE}
- **parse_valid**: ${PARSE_VALID_INT}%
- **L1**: ${L1}%
- **L2**: ${L2}%
- **L3**: ${L3}%
- **Generated**: $(date -u +%Y-%m-%dT%H:%M:%SZ)
- **Source**: $BASELINE_JSON
"

echo "$MARKDOWN"

if [[ -n "$WRITE_MARKDOWN" ]]; then
    mkdir -p "$(dirname "$WRITE_MARKDOWN")"
    echo "$MARKDOWN" > "$WRITE_MARKDOWN"
    echo "[evidence-gate-v1] Markdown written to: $WRITE_MARKDOWN" >&2
fi

# Exit code (per spec): 0 = success (PASS/Conditional/Mock), 1 = FAIL
if [[ "$VERDICT" == "FAIL" ]]; then
    exit 1
fi
exit 0
