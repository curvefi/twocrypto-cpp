"""Exact snapshot comparison helpers for C++/Boa outputs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

STATE_FIELDS = (
    "balances", "admin_balances", "xp", "D", "virtual_price", "xcp_profit",
    "lp_xcp_profit", "price_scale", "price_oracle", "last_prices", "totalSupply",
    "donation_shares", "donation_shares_unlocked", "donation_protection_expiry_ts",
    "last_donation_release_ts",
)

# These fields are outcomes rather than pool state.  They must be compared even
# when a failed action leaves every state field unchanged.
ACTION_OUTCOME_FIELDS = ("action_success",)
RESULT_OUTCOME_FIELDS = ("success", "status", "outcome", "outcomes")

_MISSING = object()


def _field_difference(
    cpp: dict[str, Any], boa: dict[str, Any], fields: tuple[str, ...]
) -> dict[str, dict[str, Any]]:
    differences: dict[str, dict[str, Any]] = {}
    for field in fields:
        cpp_value = cpp.get(field, _MISSING)
        boa_value = boa.get(field, _MISSING)
        if cpp_value != boa_value:
            differences[field] = {
                "cpp": None if cpp_value is _MISSING else cpp_value,
                "boa": None if boa_value is _MISSING else boa_value,
            }
    return differences


def _result_outcomes(result: dict[str, Any]) -> dict[str, Any]:
    """Return the enclosing result's status/outcome fields for comparison."""

    return {
        field: result[field]
        for field in RESULT_OUTCOME_FIELDS
        if field in result
    }


def _result_rows(result: dict[str, Any]) -> dict[str, dict[str, Any]]:
    rows: dict[str, dict[str, Any]] = {}
    for row in result.get("results", []):
        pool = str(row.get("pool_config") or row.get("pool_name"))
        value = row.get("result", {})
        rows[pool] = value if isinstance(value, dict) else {}
    return rows


def _states(result: dict[str, Any]) -> dict[str, list[dict[str, Any]]]:
    return {
        pool: value.get("states", [])
        for pool, value in _result_rows(result).items()
    }


def compare_snapshots(cpp: dict[str, Any], boa: dict[str, Any]) -> dict[str, Any]:
    left_results, right_results = _result_rows(cpp), _result_rows(boa)
    left, right = _states(cpp), _states(boa)
    mismatches: list[dict[str, Any]] = []
    for pool in sorted(set(left) | set(right)):
        if pool not in left or pool not in right:
            mismatches.append({"pool": pool, "error": "pool set mismatch"})
            continue

        result_fields = _field_difference(
            _result_outcomes(left_results[pool]),
            _result_outcomes(right_results[pool]),
            RESULT_OUTCOME_FIELDS,
        )
        if result_fields:
            mismatches.append({"pool": pool, "result": result_fields})

        if len(left[pool]) != len(right[pool]):
            mismatches.append({"pool": pool, "error": "state count mismatch", "cpp": len(left[pool]), "boa": len(right[pool])})
            continue
        for index, (cpp_state, boa_state) in enumerate(zip(left[pool], right[pool])):
            fields = _field_difference(cpp_state, boa_state, STATE_FIELDS)
            fields.update(_field_difference(cpp_state, boa_state, ACTION_OUTCOME_FIELDS))
            if fields:
                mismatches.append({"pool": pool, "state_index": index, "fields": fields})
    return {"equal": not mismatches, "mismatches": mismatches}


def compare_files(cpp_path: str | Path, boa_path: str | Path) -> dict[str, Any]:
    cpp = json.loads(Path(cpp_path).read_text(encoding="utf-8"))
    boa = json.loads(Path(boa_path).read_text(encoding="utf-8"))
    return compare_snapshots(cpp, boa)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cpp", type=Path)
    parser.add_argument("boa", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args(argv)
    result = compare_files(args.cpp, args.boa)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0 if result["equal"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
