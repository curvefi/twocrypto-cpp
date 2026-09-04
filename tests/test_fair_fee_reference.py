"""Behavioral checks for the oracle-informed fee reference model."""

from __future__ import annotations

import pytest

from twocrypto_parity.fair_fee_reference import (
    FEE_PRECISION,
    MAX_FEE,
    PRECISION,
    external_price,
    find_trial_xp,
    newton_d,
    quote_fair_fee,
)


AMP = 400_000  # effective A = 40 on the A_MULTIPLIER lattice
PRICE_SCALE = 100_000 * PRECISION
XP_OLD = (500_000 * PRECISION, 500_000 * PRECISION)
D = newton_d(AMP, XP_OLD)
BASE_FEE = FEE_PRECISION // 100  # 1%


def _decision(fair: int, target: int, capture: int = 0):
    trial = find_trial_xp(
        XP_OLD,
        d=D,
        amp=AMP,
        price_scale=PRICE_SCALE,
        target_price=target * PRECISION,
    )
    decision = quote_fair_fee(
        XP_OLD,
        trial,
        price_scale=PRICE_SCALE,
        d=D,
        amp=AMP,
        p_fair=fair * PRECISION,
        base_fee=BASE_FEE,
        capture=capture,
    )
    return trial, decision


@pytest.mark.parametrize(
    ("fair", "target", "direction"),
    [(105_000, 102_500, "0_to_1"), (95_000, 97_500, "1_to_0")],
)
def test_invariant_consistent_quotes_admit_both_directions(
    fair: int, target: int, direction: str
) -> None:
    trial, decision = _decision(fair, target)

    assert abs(newton_d(AMP, trial) - D) <= 1
    assert external_price(trial, D, AMP, PRICE_SCALE) == target * PRECISION
    assert decision.reason == "admitted"
    assert decision.direction == direction
    assert decision.fee == BASE_FEE
    assert decision.residual_value > 0


def test_capture_monotonically_takes_only_available_surplus() -> None:
    decisions = [
        _decision(105_000, 102_500, capture)[1]
        for capture in (0, PRECISION // 2, PRECISION)
    ]

    assert [decision.fee for decision in decisions] == sorted(
        decision.fee for decision in decisions
    )
    assert decisions[0].fee == BASE_FEE
    assert decisions[-1].fee == decisions[-1].edge_fee
    assert decisions[0].residual_value > decisions[1].residual_value
    assert 0 <= decisions[-1].residual_value < decisions[-1].value_out // FEE_PRECISION


def test_non_swap_wrong_way_and_fair_crossing_are_not_admitted() -> None:
    native = quote_fair_fee(
        XP_OLD,
        XP_OLD,
        price_scale=PRICE_SCALE,
        d=D,
        amp=AMP,
        p_fair=105_000 * PRECISION,
        base_fee=BASE_FEE,
        capture=0,
    )
    _, wrong_way = _decision(105_000, 97_500)
    _, crossed = _decision(105_000, 106_000)

    assert (native.fee, native.reason) == (0, "native_fallback")
    assert (wrong_way.fee, wrong_way.reason) == (
        MAX_FEE,
        "wrong_way_or_crossed_fair",
    )
    assert (crossed.fee, crossed.reason) == (
        MAX_FEE,
        "wrong_way_or_crossed_fair",
    )
