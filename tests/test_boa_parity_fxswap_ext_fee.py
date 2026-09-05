"""Exact uint256 C++/Boa parity authority."""

from __future__ import annotations

import functools
import json
import os
from pathlib import Path

from twocrypto_parity.cpp_pool_runner import run_cpp_pool
from twocrypto_parity.paths import build_root, core_root
from twocrypto_parity.vyper_pool_runner import run_vyper_pool


STATE_FIELDS = (
    "balances", "admin_balances", "xp", "D", "virtual_price", "xcp_profit",
    "lp_xcp_profit", "price_scale", "price_oracle", "last_prices", "totalSupply",
    "caller_lp_balance",
    "donation_shares", "donation_shares_unlocked", "donation_protection_expiry_ts",
    "last_donation_release_ts", "last_admin_fee_claim_timestamp", "timestamp",
)


def _resolve_cpp_harness() -> Path:
    if custom := os.environ.get("TWOCRYPTO_HARNESS_I"):
        return Path(custom).expanduser().resolve()
    return build_root() / "benchmark_harness_i"


@functools.lru_cache(maxsize=1)
def _require_cpp_harness() -> Path:
    harness = _resolve_cpp_harness()
    if not harness.is_file():
        raise FileNotFoundError(
            f"missing benchmark_harness_i at {harness}; build the optional parity target first "
            "or set TWOCRYPTO_BUILD_ROOT"
        )
    return harness


def _pool(name: str, policy_kind: str) -> dict:
    return {
        "name": name,
        "A": "400000",
        "gamma": "145000000000000",
        "mid_fee": "26000000",
        "out_fee": "45000000",
        "fee_gamma": "230000000000000",
        "adjustment_step_min": "1000000000000",
        "adjustment_step_max": "100000000000000000",
        "ma_time": "866",
        "reserved_profit_fraction": "5000000000",
        "admin_fee": "5000000000",
        "initial_price": "1000000000000000000",
        "policy": {"kind": policy_kind},
        "initial_liquidity": ["10000000000000000000000", "10000000000000000000000"],
    }


def _sequence() -> dict:
    wad = "1000000000000000000"
    impossible = str(2**256 - 1)
    return {
        "seed": 7,
        "sequences": [{
            "name": "fxswap_ext_fee_exact_core_actions",
            "start_timestamp": 1700000000,
            "actions": [
                {"type": "get_dy", "i": 0, "j": 1, "dx": wad},
                {"type": "get_dx", "i": 0, "j": 1, "dy": "500000000000000000", "n_iter": 5},
                {"type": "exchange", "i": 0, "j": 1, "dx": wad},
                {"type": "exchange", "i": 1, "j": 0, "dx": wad, "min_dy": impossible},
                {"type": "add_liquidity", "amounts": [wad, wad]},
                {"type": "add_liquidity", "amounts": [wad, "0"], "min_mint_amount": impossible},
                {"type": "add_liquidity", "amounts": [wad, wad], "donation": True},
                {
                    "type": "add_liquidity",
                    "amounts": ["1000000000000000000000000", "1000000000000000000000000"],
                    "donation": True,
                },
                {
                    "type": "remove_liquidity_fixed_out",
                    "token_amount": "100000000000000000000000",
                    "i": 0,
                    "amount_i": "1",
                    "min_amount_j": "0",
                },
                {"type": "remove_liquidity", "amount": "1", "caller": "other"},
                {"type": "time_travel", "seconds": 3600},
                {
                    "type": "remove_liquidity_fixed_out",
                    "token_amount": "100000000000000000000",
                    "i": 0,
                    "amount_i": "10000000000000000000",
                    "min_amount_j": "0",
                },
                {
                    "type": "remove_liquidity_one_coin",
                    "token_amount": "100000000000000000000",
                    "i": 1,
                    "min_amount": "0",
                },
                {"type": "remove_liquidity", "amount": wad, "min_amounts": ["0", "0"]},
                {"type": "unsupported_action"},
            ],
        }],
    }


def _write_json(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def _run_cpp(pools_path: Path, sequences_path: Path, output_path: Path) -> dict:
    harness = _require_cpp_harness()
    return run_cpp_pool(
        pools_path,
        sequences_path,
        output_path,
        core=core_root(),
        build=harness.parent,
        harness=harness,
    )


def _states_by_pool(result: dict) -> dict[str, list[dict]]:
    return {
        item["pool_config"]: item["result"]["states"]
        for item in result["results"]
    }


def _assert_state_equal(
    pool_name: str,
    action_idx: int,
    action: dict,
    cpp: dict,
    boa: dict,
    prev_cpp: dict | None,
    prev_boa: dict | None,
) -> None:
    mismatches = {
        field: {"cpp": cpp.get(field), "boa": boa.get(field)}
        for field in STATE_FIELDS
        if cpp.get(field) != boa.get(field)
    }
    if action_idx > 0:
        assert cpp.get("action_success") == boa.get("action_success"), {
            "pool": pool_name, "action_idx": action_idx, "action": action,
            "cpp": cpp.get("action_success"), "boa": boa.get("action_success"),
        }
        if cpp.get("action_success"):
            assert cpp.get("action_result") == boa.get("action_result"), {
                "pool": pool_name,
                "action_idx": action_idx,
                "action": action,
                "cpp_result": cpp.get("action_result"),
                "boa_result": boa.get("action_result"),
            }
    assert not mismatches, {
        "pool": pool_name, "action_idx": action_idx, "action": action,
        "previous_cpp": prev_cpp, "previous_boa": prev_boa,
        "mismatches": mismatches,
    }
    if action_idx > 0 and not cpp["action_success"]:
        for field in STATE_FIELDS:
            assert cpp[field] == prev_cpp[field]
            assert boa[field] == prev_boa[field]


def test_boa_cpp_uint256_parity_for_native_pool(tmp_path: Path) -> None:
    _require_cpp_harness()
    pool_configs = [_pool("policy_none", "none")]
    sequence = _sequence()
    pools_path, sequences_path, cpp_output, boa_output = (
        tmp_path / name for name in ("pools.json", "sequences.json", "cpp.json", "boa.json")
    )
    _write_json(pools_path, {"seed": 7, "pools": pool_configs})
    _write_json(sequences_path, sequence)
    cpp_states = _states_by_pool(_run_cpp(pools_path, sequences_path, cpp_output))
    boa_states = _states_by_pool(run_vyper_pool(str(pools_path), str(sequences_path), str(boa_output)))
    assert set(cpp_states) == set(boa_states)
    actions = sequence["sequences"][0]["actions"]
    for pool_name in sorted(cpp_states):
        assert len(cpp_states[pool_name]) == len(boa_states[pool_name])
        for action_idx, (cpp, boa) in enumerate(zip(cpp_states[pool_name], boa_states[pool_name])):
            _assert_state_equal(
                pool_name,
                action_idx,
                actions[action_idx - 1] if action_idx else {"type": "initial"},
                cpp,
                boa,
                cpp_states[pool_name][action_idx - 1] if action_idx else cpp,
                boa_states[pool_name][action_idx - 1] if action_idx else boa,
            )


def test_cpp_snapshots_keep_final_same_timestamp_action(tmp_path: Path, monkeypatch) -> None:
    wad = "1000000000000000000"
    sequence = {
        "sequences": [{
            "name": "same_timestamp_final_mutation",
            "start_timestamp": 1700000000,
            "actions": [
                {"type": "exchange", "i": 0, "j": 1, "dx": wad},
                {"type": "exchange", "i": 1, "j": 0, "dx": wad},
                {"type": "add_liquidity", "amounts": [wad, wad]},
            ],
        }],
    }
    pools_path, sequences_path, output_path = (
        tmp_path / name for name in ("pools.json", "sequences.json", "cpp.json")
    )
    _write_json(pools_path, {"pools": [_pool("snapshot_regression", "none")]})
    _write_json(sequences_path, sequence)
    def run_states(path: Path) -> list[dict]:
        return _states_by_pool(_run_cpp(pools_path, sequences_path, path))[
            "snapshot_regression"
        ]

    monkeypatch.setenv("SNAPSHOT_EVERY", "1")
    dense = run_states(output_path)
    monkeypatch.setenv("SNAPSHOT_EVERY", "2")
    sparse = run_states(tmp_path / "sparse.json")
    assert len(dense) == 4 and len(sparse) == 3
    assert sparse[-1] == dense[-1]
    assert sparse[-2] != sparse[-1]

    monkeypatch.setenv("SNAPSHOT_EVERY", "0")
    last = _run_cpp(pools_path, sequences_path, tmp_path / "last-only.json")
    assert last["results"][0]["result"]["final_state"] == dense[-1]

    sequence["sequences"][0]["actions"].append({"type": "time_travel", "seconds": 0})
    _write_json(sequences_path, sequence)
    empty_state = _run_cpp(pools_path, sequences_path, tmp_path / "empty-last.json")[
        "results"
    ][0]["result"]["final_state"]
    assert empty_state["action_success"] is True and "action_result" not in empty_state
    sequence["sequences"][0]["actions"][-1] = {"type": "unsupported_action"}
    _write_json(sequences_path, sequence)
    failed_state = _run_cpp(pools_path, sequences_path, tmp_path / "failed-last.json")[
        "results"
    ][0]["result"]["final_state"]
    assert failed_state["action_success"] is False
    assert "action_result" not in failed_state and "error" in failed_state


def test_boa_cpp_pool_integrated_yb_policy_parity(tmp_path: Path) -> None:
    _require_cpp_harness()
    pool = _pool("policy_yieldbasis", "compiled")
    pool["policy"] = {
        "kind": "compiled",
        "reference_kind": "yb_twocrypto_policy",
        "params": [
            "3600",
            "7200",
            "2000000000000000000",
            "0",
            "100000000000000",
            "6000000000000000",
        ],
    }
    sequence = {
        "seed": 17,
        "sequences": [{
            "name": "yieldbasis_pool_update_and_scale_target",
            "start_timestamp": 1700000000,
            "actions": [
                {
                    "type": "exchange",
                    "i": 0,
                    "j": 1,
                    "dx": "1000000000000000000000",
                },
                {"type": "time_travel", "seconds": 1800},
                {
                    "type": "exchange",
                    "i": 1,
                    "j": 0,
                    "dx": "100000000000000000000",
                },
                {"type": "time_travel", "seconds": 1800},
                {
                    "type": "add_liquidity",
                    "amounts": ["1000000000000000000", "1000000000000000000"],
                },
                {
                    "type": "exchange",
                    "i": 0,
                    "j": 1,
                    "dx": "100000000000000000000",
                },
            ],
        }],
    }
    pools_path, sequences_path, cpp_output, boa_output = (
        tmp_path / name
        for name in ("yb-pools.json", "yb-sequences.json", "yb-cpp.json", "yb-boa.json")
    )
    _write_json(pools_path, {"seed": 17, "pools": [pool]})
    _write_json(sequences_path, sequence)
    cpp_states = _states_by_pool(_run_cpp(pools_path, sequences_path, cpp_output))
    boa_states = _states_by_pool(
        run_vyper_pool(str(pools_path), str(sequences_path), str(boa_output))
    )
    assert set(cpp_states) == set(boa_states) == {"policy_yieldbasis"}
    actions = sequence["sequences"][0]["actions"]
    for action_idx, (cpp, boa) in enumerate(
        zip(cpp_states["policy_yieldbasis"], boa_states["policy_yieldbasis"])
    ):
        _assert_state_equal(
            "policy_yieldbasis",
            action_idx,
            actions[action_idx - 1] if action_idx else {"type": "initial"},
            cpp,
            boa,
            cpp_states["policy_yieldbasis"][action_idx - 1] if action_idx else cpp,
            boa_states["policy_yieldbasis"][action_idx - 1] if action_idx else boa,
        )
