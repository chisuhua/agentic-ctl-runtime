#!/usr/bin/env python3
# tools/baseline_schema_validate.py
# Phase 7a baseline JSON schema validator (per OpenSpec phase-7a-baseline-retest-v2)
# Decision 3: jsonschema Draft202012Validator (对齐 ADR-0073 C++ validator)
# Decision 8: baseline.json schema 对齐 tools/baseline/measure_prompt_baseline.py (llms + task_success v2)

import argparse
import json
import sys

try:
    from jsonschema import Draft202012Validator
except ImportError:
    print("ERROR: jsonschema not installed. Run: pip install -r requirements.txt",
          file=sys.stderr)
    sys.exit(2)


# baseline.json schema (v2, 对齐 tools/baseline/measure_prompt_baseline.py + 新增 task_success L1/L2/L3)
BASELINE_SCHEMA = {
    "$schema": "https://json-schema.org/draft/2020-12/schema",
    "type": "object",
    "required": ["baseline_id", "llms", "mock_mode", "generated_at", "summary"],
    "properties": {
        "baseline_id": {"type": "string"},
        "golden_tasks": {"type": "integer", "minimum": 1},
        "mock_mode": {"type": "boolean"},
        "llms": {
            "type": "object",
            "minProperties": 1,
            "patternProperties": {
                "^[A-Za-z0-9._/-]+$": {
                    "type": "object",
                    "required": ["parse_valid", "task_success"],
                    "properties": {
                        "parse_valid": {"type": "number", "minimum": 0.0, "maximum": 1.0},
                        "task_success": {
                            "type": "object",
                            "required": ["L1", "L2", "L3"],
                            "properties": {
                                "L1": {"type": "number", "minimum": 0.0, "maximum": 1.0},
                                "L2": {"type": "number", "minimum": 0.0, "maximum": 1.0},
                                "L3": {"type": "number", "minimum": 0.0, "maximum": 1.0},
                            },
                        },
                    },
                },
            },
        },
        "models_partial": {
            "type": "array",
            "items": {"type": "string"},
        },
        "generated_at": {"type": "string"},
        "summary": {
            "type": "object",
            "required": ["samples", "avg_parse_valid", "avg_task_success"],
            "properties": {
                "samples": {"type": "integer", "minimum": 0},
                "avg_parse_valid": {"type": "number", "minimum": 0.0, "maximum": 1.0},
                "avg_task_success": {
                    "type": "object",
                    "required": ["L1", "L2", "L3"],
                    "properties": {
                        "L1": {"type": "number", "minimum": 0.0, "maximum": 1.0},
                        "L2": {"type": "number", "minimum": 0.0, "maximum": 1.0},
                        "L3": {"type": "number", "minimum": 0.0, "maximum": 1.0},
                    },
                },
            },
        },
        "samples": {
            "type": "array",
        },
    },
}


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Phase 7a baseline JSON schema validator (Draft 2020-12)"
    )
    parser.add_argument("baseline_json", help="Path to baseline JSON file")
    args = parser.parse_args()

    try:
        with open(args.baseline_json, "r", encoding="utf-8") as fp:
            data = json.load(fp)
    except (FileNotFoundError, json.JSONDecodeError) as exc:
        print(f"ERROR: cannot load {args.baseline_json}: {exc}", file=sys.stderr)
        return 1

    validator = Draft202012Validator(BASELINE_SCHEMA)
    errors = sorted(validator.iter_errors(data), key=lambda e: list(e.path))

    if errors:
        print(f"✗ invalid: {len(errors)} error(s) in {args.baseline_json}", file=sys.stderr)
        for err in errors:
            path = "/".join(str(p) for p in err.absolute_path) or "<root>"
            print(f"  - {path}: {err.message}", file=sys.stderr)
        return 1

    print(f"✓ valid: {args.baseline_json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
