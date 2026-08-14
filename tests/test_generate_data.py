from __future__ import annotations

import random

from twocrypto_parity.generate_data import (
    BPS,
    LP_RESERVE,
    MILLI_WAD,
    REMOVE_BPS,
    SWAP_DIRECTION_BATCH,
    WAD,
    SequenceState,
    generate_action_sequences,
    regular_add_liquidity_action,
    remove_liquidity_action,
)


def _pool(initial0: int, initial1: int) -> dict:
    return {"initial_liquidity": [str(initial0), str(initial1)]}


def test_generated_swaps_are_batched_and_below_one_percent_initial_balance() -> None:
    random.seed(123)
    pool_configs = [_pool(10_000 * WAD, 10_000 * WAD)]
    sequence = generate_action_sequences(90, 1_700_000_000, pool_configs)[0]
    exchanges = [action for action in sequence["actions"] if action["type"] == "exchange"]
    assert len(exchanges) >= 20
    first_direction = exchanges[0]["i"]
    initial_balances = [int(value) for value in pool_configs[0]["initial_liquidity"]]
    for idx, action in enumerate(exchanges):
        expected_direction = (first_direction + idx // SWAP_DIRECTION_BATCH) % 2
        assert action["i"] == expected_direction
        assert int(action["dx"]) <= initial_balances[action["i"]] * 100 // BPS


def test_remove_liquidity_generator_respects_owned_lp_reserve() -> None:
    random.seed(321)
    initial_balances = [[10_000 * WAD, 10_000 * WAD]]
    state = SequenceState(
        owned_lp=LP_RESERVE,
        total_lp=LP_RESERVE,
        balances=[row.copy() for row in initial_balances],
        initial_balances=[row.copy() for row in initial_balances],
    )
    assert remove_liquidity_action(state) is None
    regular_add_liquidity_action(state)
    budget_before = state.remove_budget()
    action = remove_liquidity_action(state)
    assert action is not None
    amount = int(action["amount"])
    assert MILLI_WAD <= amount <= budget_before
    assert amount <= budget_before * REMOVE_BPS[1] // BPS
    assert state.owned_lp >= LP_RESERVE


def test_seeded_fixture_generation_does_not_change_caller_rng() -> None:
    from twocrypto_parity.generate_data import fixture_bundle

    random.seed(99)
    before = random.getstate()
    pools_a, sequences_a = fixture_bundle(seed=7, num_pools=2, trades=8)
    assert random.getstate() == before
    pools_b, sequences_b = fixture_bundle(seed=7, num_pools=2, trades=8)
    assert pools_a == pools_b
    assert sequences_a == sequences_b
