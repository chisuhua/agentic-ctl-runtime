#!/usr/bin/env python3
# scripts/measure-baseline.py
# Phase 7a baseline retest measurement entry (per OpenSpec phase-7a-baseline-retest-v2)
# Decision 7: --concurrency N (default 3) parallel model calls
# Decision 8: baseline.json schema aligned with tools/baseline/measure_prompt_baseline.py
# Metis H6: fixtures 独立 (scripts/fixtures/baseline_responses.json), 不反向依赖测试

import argparse
import concurrent.futures
import json
import sys
from datetime import datetime, timezone
from pathlib import Path


def load_golden_tasks(tasks_dir: Path) -> list:
    """加载 lib/prompt/golden/*.json 任务列表 (per Oracle B1 修复, 复用既有 JSON)"""
    tasks = []
    for f in sorted(tasks_dir.glob("*.json")):
        with open(f, "r", encoding="utf-8") as fp:
            tasks.append({"task_id": f.stem, "data": json.load(fp)})
    return tasks


def load_mock_responses(fixtures_path: Path) -> dict:
    """加载确定性 mock 响应 fixture (per Metis H6)"""
    if not fixtures_path.exists():
        return {}
    with open(fixtures_path, "r", encoding="utf-8") as fp:
        return json.load(fp)


def mock_call_model(model: str, task_id: str, fixtures: dict) -> dict:
    """mock mode: 从 fixtures 读取确定性响应; real mode: 暂为 stub (per Metis F4)"""
    key = f"{model}:{task_id}"
    if key in fixtures:
        return fixtures[key]
    # 默认 mock: 95% parse_valid + 80% L1/L2/L3 (覆盖 PASS 场景)
    return {
        "response": f"mock response for {key}",
        "parse_valid": True,
        "task_success": {"L1": True, "L2": True, "L3": True},
    }


def call_model_real(model: str, task_id: str, prompt: str) -> dict:
    """real mode HTTP client stub (per Metis F4 仅 OpenAI-compatible 接口预留)"""
    raise NotImplementedError(
        f"real mode HTTP client for {model} not implemented (per Decision F4 stub)"
    )


def measure_sample(model: str, task: dict, dimension: str, prompt_version: str,
                   mode: str, fixtures: dict) -> dict:
    """测量单个 (model, task, dimension, prompt_version) 样本"""
    if mode == "mock":
        result = mock_call_model(model, task["task_id"], fixtures)
    else:
        result = call_model_real(model, task["task_id"], prompt_version)

    return {
        "model": model,
        "task_id": task["task_id"],
        "dimension": dimension,
        "prompt_version": prompt_version,
        "parse_valid": result.get("parse_valid", False),
        "task_success": result.get("task_success", {}),
        "response": result.get("response", ""),
    }


def aggregate_results(samples: list, models: list) -> dict:
    """聚合 samples → baseline.json (per Decision 8 schema 对齐)"""
    n = len(samples)
    parse_valid_count = sum(1 for s in samples if s["parse_valid"])
    parse_valid = parse_valid_count / n if n > 0 else 0.0

    # per-model task_success L1/L2/L3 平均
    llms_stats = {}
    for model in models:
        model_samples = [s for s in samples if s["model"] == model]
        if not model_samples:
            llms_stats[model] = {"parse_valid": 0.0, "task_success": {"L1": 0.0, "L2": 0.0, "L3": 0.0}}
            continue
        mpv = sum(1 for s in model_samples if s["parse_valid"]) / len(model_samples)
        ml1 = sum(1 for s in model_samples if s.get("task_success", {}).get("L1", False)) / len(model_samples)
        ml2 = sum(1 for s in model_samples if s.get("task_success", {}).get("L2", False)) / len(model_samples)
        ml3 = sum(1 for s in model_samples if s.get("task_success", {}).get("L3", False)) / len(model_samples)
        llms_stats[model] = {
            "parse_valid": mpv,
            "task_success": {"L1": ml1, "L2": ml2, "L3": ml3},
        }

    # 3-model avg
    avg_parse = sum(s["parse_valid"] for s in llms_stats.values()) / len(llms_stats) if llms_stats else 0.0
    avg_l1 = sum(s["task_success"]["L1"] for s in llms_stats.values()) / len(llms_stats) if llms_stats else 0.0
    avg_l2 = sum(s["task_success"]["L2"] for s in llms_stats.values()) / len(llms_stats) if llms_stats else 0.0
    avg_l3 = sum(s["task_success"]["L3"] for s in llms_stats.values()) / len(llms_stats) if llms_stats else 0.0

    return {
        "baseline_id": f"baseline-real-{datetime.now(timezone.utc).strftime('%Y-%m-%d')}",
        "golden_tasks": len(set(s["task_id"] for s in samples)),
        "mock_mode": False,  # 由调用方覆盖 (mock mode 设为 true)
        "llms": llms_stats,  # per Decision 8: 复用字段 llms (非 models)
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "summary": {
            "samples": n,
            "avg_parse_valid": avg_parse,
            "avg_task_success": {"L1": avg_l1, "L2": avg_l2, "L3": avg_l3},
        },
    }


def check_env(models: list, mode: str) -> int:
    """per spec: --check-env 模式验证 API 可达性"""
    if mode == "mock":
        print(f"[check-env] mock mode, skipping API reachability for {models}")
        return 0
    print(f"[check-env] real mode: checking {len(models)} models...")
    # real mode stub (per Metis F4)
    print(f"[check-env] WARNING: real mode HTTP client not implemented (Decision F4)")
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Phase 7a baseline measurement (per ADR-0074 D5 v2 semantics)"
    )
    parser.add_argument("--models", help="CSV of model names")
    parser.add_argument("--mode", choices=["real", "mock"], default="mock",
                        help="real | mock (default: mock)")
    parser.add_argument("--prompts", help="CSV of prompt versions (e.g. v1_schema,v2_fewshot,v3_two_phase)")
    parser.add_argument("--dimensions", help="CSV of eval dimensions (e.g. structured,tool_call,error_recovery,long_context)")
    parser.add_argument("--tasks-dir", type=Path,
                        help="Directory of golden task JSONs (default: lib/prompt/golden/)")
    parser.add_argument("--output", type=Path, help="Output JSON file path")
    parser.add_argument("--concurrency", type=int, default=3,
                        help="Parallel model call count (default: 3 per Decision 7)")
    parser.add_argument("--check-env", action="store_true",
                        help="Check model API reachability and exit")
    parser.add_argument("--fixtures", type=Path,
                        default=Path(__file__).parent / "fixtures" / "baseline_responses.json",
                        help="Mock fixtures path (per Metis H6)")
    args = parser.parse_args()

    # --check-env 模式
    if args.check_env:
        if not args.models:
            print("ERROR: --models required for --check-env", file=sys.stderr)
            return 2
        models = [m.strip() for m in args.models.split(",")]
        return check_env(models, args.mode)

    # 必填参数校验
    if not all([args.models, args.tasks_dir, args.output]):
        print("ERROR: --models, --tasks-dir, --output required (or use --check-env)", file=sys.stderr)
        return 2

    models = [m.strip() for m in args.models.split(",")]
    dimensions = [d.strip() for d in (args.dimensions or "structured,tool_call,error_recovery,long_context").split(",")]
    prompt_versions = [p.strip() for p in (args.prompts or "v1_schema,v2_fewshot,v3_two_phase").split(",")]

    # 加载 golden tasks
    if not args.tasks_dir.exists():
        print(f"ERROR: tasks dir not found: {args.tasks_dir}", file=sys.stderr)
        return 2
    tasks = load_golden_tasks(args.tasks_dir)
    if not tasks:
        print(f"ERROR: no golden tasks found in {args.tasks_dir}", file=sys.stderr)
        return 2

    # 加载 fixtures (mock mode)
    fixtures = {}
    if args.mode == "mock":
        fixtures = load_mock_responses(args.fixtures)

    # 生成 samples
    samples = []
    jobs = []
    for model in models:
        for task in tasks:
            for dim in dimensions:
                for pv in prompt_versions:
                    jobs.append((model, task, dim, pv))

    # Decision 7: concurrent model calls
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.concurrency) as executor:
        future_to_job = {
            executor.submit(measure_sample, model, task, dim, pv, args.mode, fixtures): (model, task, dim, pv)
            for (model, task, dim, pv) in jobs
        }
        for future in concurrent.futures.as_completed(future_to_job):
            try:
                samples.append(future.result())
            except Exception as exc:
                model, task, dim, pv = future_to_job[future]
                samples.append({
                    "model": model,
                    "task_id": task["task_id"],
                    "dimension": dim,
                    "prompt_version": pv,
                    "parse_valid": False,
                    "task_success": {"L1": False, "L2": False, "L3": False},
                    "error": str(exc),
                })

    # 聚合 + 输出 (per Decision 8 schema)
    output = aggregate_results(samples, models)
    output["mock_mode"] = (args.mode == "mock")
    output["samples"] = samples  # 全量 samples (供 evidence-gate 决策)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as fp:
        json.dump(output, fp, indent=2, ensure_ascii=False)

    # 摘要打印
    s = output["summary"]
    print(f"[measure-baseline] samples={s['samples']} avg_parse={s['avg_parse_valid']:.3f} "
          f"L1={s['avg_task_success']['L1']:.3f} L2={s['avg_task_success']['L2']:.3f} "
          f"L3={s['avg_task_success']['L3']:.3f} → {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
