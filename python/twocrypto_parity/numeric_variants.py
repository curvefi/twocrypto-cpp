"""Run typed pool parity adapters over one input set.

This module never builds or writes beside the pool repository. ``--out`` is
mandatory and is the only directory created. A ``--harness`` override is
intentionally limited to one selected mode; omitted overrides resolve each
mode's typed parity adapter from the pool build directory.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
from pathlib import Path
from typing import Any, Iterable

from .cpp_pool_runner import MODES, run_cpp_pool

METRICS = ("balances", "D", "virtual_price", "totalSupply", "price_scale")
PACKAGE_VERSION = "0.1.0"
REFERENCE_REVISION = "2c645ca604a4a0878e08f2f1581e5c4ae1c8f8d4"


def version_string() -> str:
    return (
        f"twocrypto-parity {PACKAGE_VERSION} "
        f"twocrypto-ng={REFERENCE_REVISION} python={platform.python_version()}"
    )


def _extract_states(results: dict[str, Any]) -> dict[str, list[dict[str, Any]]]:
    states: dict[str, list[dict[str, Any]]] = {}
    for test in results.get("results", []):
        key = test.get("pool_config") or test.get("pool_name")
        result = test.get("result", {})
        values = result.get("states")
        if values is None and result.get("final_state") is not None:
            values = [result["final_state"]]
        states[str(key)] = values or []
    return states


def _as_int(value: Any) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        try:
            return int(float(value))
        except (TypeError, ValueError):
            return 0


def _relative_percent(actual: int, baseline: int) -> float:
    if baseline == 0:
        return 0.0 if actual == 0 else float("inf")
    return abs(actual - baseline) * 100.0 / abs(baseline)


def final_differences(baseline: dict[str, Any], variant: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    base_states = _extract_states(baseline)
    variant_states = _extract_states(variant)
    relative: dict[str, Any] = {}
    absolute: dict[str, Any] = {}
    stats = {metric: {"count": 0, "max_rel_pct": 0.0, "sum_rel_pct": 0.0} for metric in METRICS}
    for pool, base_rows in base_states.items():
        variant_rows = variant_states.get(pool, [])
        if not base_rows or not variant_rows:
            continue
        base, actual = base_rows[-1], variant_rows[-1]
        rel_pool: dict[str, Any] = {}
        abs_pool: dict[str, Any] = {}
        for metric in METRICS:
            if metric not in base or metric not in actual:
                continue
            left, right = base[metric], actual[metric]
            if isinstance(left, list) and isinstance(right, list):
                rel_values = [_relative_percent(_as_int(r), _as_int(l)) for l, r in zip(left, right)]
                abs_values = [abs(_as_int(r) - _as_int(l)) for l, r in zip(left, right)]
            else:
                rel_values = _relative_percent(_as_int(right), _as_int(left))
                abs_values = abs(_as_int(right) - _as_int(left))
            rel_pool[metric] = rel_values
            abs_pool[metric] = abs_values
            values = rel_values if isinstance(rel_values, list) else [rel_values]
            for value in values:
                stats[metric]["count"] += 1
                if value != float("inf"):
                    stats[metric]["sum_rel_pct"] += value
                stats[metric]["max_rel_pct"] = max(stats[metric]["max_rel_pct"], value)
        relative[pool] = rel_pool
        absolute[pool] = abs_pool
    for metric, row in stats.items():
        row["mean_rel_pct"] = row.pop("sum_rel_pct") / max(row["count"], 1)
    return relative, absolute, stats


def run_variants(
    pools_file: str | os.PathLike[str],
    sequences_file: str | os.PathLike[str],
    out: str | os.PathLike[str],
    *,
    modes: Iterable[str] = MODES,
    core: str | os.PathLike[str] | None = None,
    build: str | os.PathLike[str] | None = None,
    harness: str | os.PathLike[str] | None = None,
    threads: int | None = None,
    final_only: bool = False,
    snapshot_every: int | None = None,
) -> dict[str, Any]:
    selected = tuple(modes)
    if not selected or any(mode not in MODES for mode in selected):
        raise ValueError(f"modes must be drawn from {', '.join(MODES)}")
    if len(set(selected)) != len(selected):
        raise ValueError("modes must not contain duplicates")
    if harness is not None and len(selected) != 1:
        raise ValueError(
            "--harness is a single executable override; use it with exactly one "
            "selected mode or omit it for mode-specific harness resolution"
        )
    output_dir = Path(out).expanduser().resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    env_keys = ("CPP_THREADS", "SAVE_LAST_ONLY", "SNAPSHOT_EVERY")
    previous = {key: os.environ.get(key) for key in env_keys}
    results: dict[str, dict[str, Any]] = {}
    try:
        if threads is not None:
            os.environ["CPP_THREADS"] = str(threads)
        if snapshot_every is not None:
            os.environ["SNAPSHOT_EVERY"] = str(snapshot_every)
            os.environ.pop("SAVE_LAST_ONLY", None)
        elif final_only:
            os.environ["SAVE_LAST_ONLY"] = "1"
        for mode in selected:
            output_file = output_dir / f"cpp_{mode}_combined.json"
            results[mode] = run_cpp_pool(
                mode,
                pools_file,
                sequences_file,
                output_file,
                core=core,
                build=build,
                harness=harness,
            )
    finally:
        for key, value in previous.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value

    summary: dict[str, Any] = {
        "modes": list(selected),
        "tests": len(results[selected[0]].get("results", [])),
        "outputs": {mode: f"cpp_{mode}_combined.json" for mode in selected},
    }
    baseline = results.get("i")
    if baseline is not None:
        comparisons = {}
        for mode in selected:
            if mode == "i":
                continue
            rel, absolute, stats = final_differences(baseline, results[mode])
            comparisons[mode] = {"relative_percent": rel, "absolute": absolute, "stats": stats}
        summary["comparisons_vs_i"] = comparisons
    (output_dir / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return summary


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--version", action="version", version=version_string())
    parser.add_argument(
        "--pools-file",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "fixtures" / "pools.json",
    )
    parser.add_argument(
        "--sequences-file",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "fixtures" / "sequences.json",
    )
    parser.add_argument("--modes", default="i,d,f,ld")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--core", type=Path, default=None)
    parser.add_argument("--build", type=Path, default=None)
    parser.add_argument("--harness", type=Path, default=None)
    parser.add_argument("--threads", type=int, default=None)
    parser.add_argument("--final-only", action="store_true")
    parser.add_argument("--snapshot-every", type=int, default=None)
    args = parser.parse_args(argv)
    selected_modes = tuple(item for item in args.modes.split(",") if item)
    if args.harness is not None and len(selected_modes) != 1:
        parser.error(
            "--harness is a single executable override; use it with exactly one "
            "selected mode or omit it for mode-specific harness resolution"
        )
    run_variants(
        args.pools_file,
        args.sequences_file,
        args.out,
        modes=selected_modes,
        core=args.core,
        build=args.build,
        harness=args.harness,
        threads=args.threads,
        final_only=args.final_only,
        snapshot_every=args.snapshot_every,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
