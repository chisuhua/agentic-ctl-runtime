// include/agenticdsl/contract/causal_order.h
// 功能描述: 跨 BusEvent 因果关系判定函数 (ADR-0037 §4.1 — Change C 完成项 T6)。
//          header-only 纯函数实现, 三规则优先级: L2 显式因果链 → L1 causal_time → Concurrent。
//
//          ⚠️ 1-hop 限制 (ADR-0037 line 538 注明): 函数仅判两个事件之间的直接因果关系,
//          传递性由调用方链式推导 — 不实现 DAG 传递闭包。
//
// 设计依据: ADR-0037 §决策 3 + OpenSpec change 2026-09-10-adr-0037-causal-ordering-completion
// 作者: HydraForge / ADR-0037 causal-ordering-completion
// 最后修改日期: 2026-09-10
#pragma once

#include "agenticdsl/contract/bus_event.h"

namespace agenticdsl::event {

/**
 * @brief 两事件因果关系判定结果。
 *
 * - ABeforeB: a happens-before b (a → b, a 在 b 之前)
 * - BBeforeA: b happens-before a (b → a, b 在 a 之前)
 * - Concurrent: 无因果关系 (并发或语义不可比)
 */
enum class CausalRelation { ABeforeB, BBeforeA, Concurrent };

/**
 * @brief 判定两 BusEvent 因果关系。
 *
 * 判定规则 (per ADR-0037 §4.1, 优先级 L2 → L1 → 默认):
 * 1. **L2 显式因果链**: a.payload.trace_id == b.payload.parent_trace → ABeforeB;
 *    反向同理。
 * 2. **L1 causal_time 回退**: 两者 causal_time 都非 0 (排除 sentinel) → 时间小者先。
 * 3. **Concurrent 默认**: 否则。
 *
 * @note ⚠️ 仅判 1-hop 直接因果 (ADR-0037 line 538)。传递性由调用方链式推导:
 *   causal_order(a, b) == ABeforeB && causal_order(b, c) == ABeforeB
 *   => a happens-before c (调用方手动推导)
 *   本函数不实现 DAG 传递闭包。
 *
 * @param a 第一个事件
 * @param b 第二个事件
 * @return CausalRelation 枚举值
 */
inline CausalRelation causal_order(const BusEvent& a, const BusEvent& b) {
    // 规则 1: L2 显式因果链 (最高优先级)
    // BusEvent 无顶层 trace_id / parent_trace 字段 — 全部在 payload (ToolResult) 内
    if (a.payload.trace_id.has_value() && b.payload.parent_trace.has_value() &&
        *a.payload.trace_id == *b.payload.parent_trace) {
        return CausalRelation::ABeforeB;
    }
    if (b.payload.trace_id.has_value() && a.payload.parent_trace.has_value() &&
        *b.payload.trace_id == *a.payload.parent_trace) {
        return CausalRelation::BBeforeA;
    }

    // 规则 2: L1 causal_time 回退
    // causal_time == 0 是 BusEvent 字段默认 sentinel (未填充) — 必须排除,
    // 否则两个未填充事件会误判为 ABeforeB (因 0 < 0 不成立但 0 == 0 会落入 Concurrent,
    // 而单边 0 会被误判)。最稳健: 两者都非 0 才回退。
    if (a.causal_time != 0 && b.causal_time != 0) {
        if (a.causal_time < b.causal_time) return CausalRelation::ABeforeB;
        if (b.causal_time < a.causal_time) return CausalRelation::BBeforeA;
        // 相等: 落到 Concurrent (实际 atomic tick 不可能产生相等 causal_time)
    }

    // 规则 3: Concurrent 默认
    return CausalRelation::Concurrent;
}

/**
 * @brief happens_before 便捷 wrapper: a → b?
 *
 * @param a 因果链上游事件
 * @param b 因果链下游事件
 * @return true iff causal_order(a, b) == CausalRelation::ABeforeB
 */
inline bool happens_before(const BusEvent& a, const BusEvent& b) {
    return causal_order(a, b) == CausalRelation::ABeforeB;
}

}  // namespace agenticdsl::event