from __future__ import annotations

from twocrypto_parity.compare import compare_snapshots


def _output(*, action_success: bool, result: dict[str, object] | None = None) -> dict:
    state = {
        "balances": ["1", "2"],
        "action_success": action_success,
    }
    return {
        "results": [
            {
                "pool_config": "pool-0",
                "result": {
                    "success": True,
                    "states": [state],
                    **(result or {}),
                },
            }
        ]
    }


def test_compare_rejects_one_sided_action_outcome_mismatch() -> None:
    cpp = _output(action_success=False)
    boa = _output(action_success=True)

    comparison = compare_snapshots(cpp, boa)

    assert comparison["equal"] is False
    assert comparison["mismatches"] == [
        {
            "pool": "pool-0",
            "state_index": 0,
            "fields": {
                "action_success": {"cpp": False, "boa": True},
            },
        }
    ]


def test_compare_includes_enclosing_result_outcomes() -> None:
    cpp = _output(action_success=True, result={"status": "rejected", "outcomes": [False]})
    boa = _output(action_success=True, result={"status": "accepted", "outcomes": [True]})

    comparison = compare_snapshots(cpp, boa)

    assert comparison["equal"] is False
    assert comparison["mismatches"][0] == {
        "pool": "pool-0",
        "result": {
            "status": {"cpp": "rejected", "boa": "accepted"},
            "outcomes": {"cpp": [False], "boa": [True]},
        },
    }
