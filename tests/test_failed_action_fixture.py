from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_failed_action_fixture_records_atomicity_case() -> None:
    payload = json.loads((ROOT / "fixtures" / "failed_action.json").read_text(encoding="utf-8"))
    sequence = payload["sequence"]
    assert payload["seed"] == 7
    assert sequence["expected_failed_action_index"] == 1
    assert sequence["actions"][0]["type"] == "unsupported_action"
    assert "start_timestamp" in sequence
