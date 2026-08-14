"""Generate deterministic pool/action fixtures for C++ and Boa parity.

All generated files are caller-owned outputs.  The command requires ``--out``
and records the seed in each JSON envelope; it never writes to the package,
reference checkout, or core build directory.
"""

from __future__ import annotations

import argparse
import json
import math
import random
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any

Action = dict[str, Any]
PoolConfig = dict[str, Any]

WAD = 10**18
MILLI_WAD = 10**15
FEE_SCALE = 10**10
BPS = 10_000
DEFAULT_START_TS = 1_700_000_000
MINIMUM_LIQUIDITY = 10**4
INITIAL_POOL_TOKENS = (3_000, 10_000)
INITIAL_LIQUIDITY_JITTER_BPS = (9_500, 10_500)
SWAP_INITIAL_BPS = (10, 20)
SWAP_OUTPUT_CAP_BPS = 500
SWAP_DIRECTION_BATCH = 10
REGULAR_ADD_MILLI = (50, 5_000)
DONATION_MILLI = (1, 50)
DONATION_CAP_RATIO_ESTIMATE = 8 * 10**16
DONATION_MAX_TRIES = 16
LP_RESERVE = WAD
MIN_REMOVE_LP = MILLI_WAD
REMOVE_BPS = (1, 25)
ADJUSTMENT_STEP_MIN = 10**12
ADJUSTMENT_STEP_MAX_BPS = (100, 2_000)
RESERVED_PROFIT_FRACTION_BPS = (0, BPS)
ADMIN_FEE_BPS = (0, int(0.9 * BPS))
TIME_TRAVEL_EVERY_STEPS = 4
TIME_TRAVEL_SECONDS = (60, 3_600)
FORCED_ACTION_KINDS = ("exchange", "add_liquidity", "donation", "remove_liquidity")
RANDOM_ACTION_MIX = (
    (0.45, "exchange"),
    (0.68, "add_liquidity"),
    (0.85, "donation"),
    (1.00, "remove_liquidity"),
)
POLICY_KINDS = ("none", "twocrypto_policy", "zero_stub", "oracle_x2")


@dataclass
class SequenceState:
    """Conservative state used to keep generated actions executable."""

    owned_lp: int
    total_lp: int
    balances: list[list[int]]
    initial_balances: list[list[int]]
    donation_lp: int = 0
    swap_count: int = 0
    first_swap_direction: int = 0

    @classmethod
    def from_pools(cls, pool_configs: list[PoolConfig] | None) -> "SequenceState":
        balances = (
            [
                [int(pool["initial_liquidity"][0]), int(pool["initial_liquidity"][1])]
                for pool in pool_configs
            ]
            if pool_configs
            else [[1_000 * WAD, 1_000 * WAD]]
        )
        min_initial_balance = min(min(row) for row in balances)
        owned_lp = max(LP_RESERVE, (min_initial_balance - MINIMUM_LIQUIDITY) // 2)
        return cls(
            owned_lp=owned_lp,
            total_lp=owned_lp,
            balances=[row.copy() for row in balances],
            initial_balances=[row.copy() for row in balances],
            first_swap_direction=random.randint(0, 1),
        )

    def record_deposit(self, amounts: tuple[int, int]) -> None:
        for balances in self.balances:
            balances[0] += amounts[0]
            balances[1] += amounts[1]

    def record_regular_add(self, amounts: tuple[int, int]) -> None:
        minted_estimate = max(1, min(amounts) // 2)
        self.record_deposit(amounts)
        self.owned_lp += minted_estimate
        self.total_lp += minted_estimate

    def can_accept_donation(self, minted_estimate: int) -> bool:
        new_total = self.total_lp + minted_estimate
        new_donation = self.donation_lp + minted_estimate
        return new_total > 0 and new_donation * WAD // new_total <= DONATION_CAP_RATIO_ESTIMATE

    def record_donation(self, amounts: tuple[int, int], minted_estimate: int) -> None:
        self.record_deposit(amounts)
        self.donation_lp += minted_estimate
        self.total_lp += minted_estimate

    def remove_budget(self) -> int:
        return max(0, self.owned_lp - LP_RESERVE)

    def record_remove(self, amount: int) -> None:
        old_total_lp = max(1, self.total_lp)
        for balances in self.balances:
            balances[0] = max(0, balances[0] - balances[0] * amount // old_total_lp)
            balances[1] = max(0, balances[1] - balances[1] * amount // old_total_lp)
        self.owned_lp -= amount
        self.total_lp = max(0, self.total_lp - amount)

    def swap_directions(self) -> list[int]:
        batch = self.swap_count // SWAP_DIRECTION_BATCH
        direction = (self.first_swap_direction + batch) % 2
        return [direction, 1 - direction]

    def swap_bounds(self, coin_in: int) -> tuple[int, int] | None:
        coin_out = 1 - coin_in
        min_initial_input = min(row[coin_in] for row in self.initial_balances)
        lower = min_initial_input * SWAP_INITIAL_BPS[0] // BPS
        upper = min_initial_input * SWAP_INITIAL_BPS[1] // BPS
        output_cap = min(
            balances[coin_out] * SWAP_OUTPUT_CAP_BPS // BPS for balances in self.balances
        )
        upper = min(upper, output_cap)
        lower_units = ceil_to_milli_wad(lower) // MILLI_WAD
        upper_units = upper // MILLI_WAD
        return (lower_units * MILLI_WAD, upper_units * MILLI_WAD) if lower_units <= upper_units else None

    def record_swap(self, coin_in: int, dx: int) -> None:
        coin_out = 1 - coin_in
        for balances in self.balances:
            dy_estimate = dx * balances[coin_out] // (balances[coin_in] + dx)
            balances[coin_in] += dx
            balances[coin_out] = max(0, balances[coin_out] - dy_estimate)
        self.swap_count += 1


def ceil_to_milli_wad(amount: int) -> int:
    return ((amount + MILLI_WAD - 1) // MILLI_WAD) * MILLI_WAD


def random_milli_wad(bounds: tuple[int, int]) -> int:
    return random.randint(bounds[0], bounds[1]) * MILLI_WAD


def jitter_bps(amount: int, bounds: tuple[int, int]) -> int:
    return amount * random.randint(bounds[0], bounds[1]) // BPS


def random_fee_fraction(bounds: tuple[int, int]) -> int:
    return random.randint(bounds[0], bounds[1]) * FEE_SCALE // BPS


def generate_pool_configs(num_pools: int = 3, policy_kind: str = "none") -> list[PoolConfig]:
    if num_pools <= 0:
        raise ValueError("num_pools must be positive")
    if policy_kind not in POLICY_KINDS:
        raise ValueError(f"unknown policy kind: {policy_kind}")
    pools: list[PoolConfig] = []
    base_liquidity = random.randint(*INITIAL_POOL_TOKENS) * WAD
    for i in range(num_pools):
        mid_fee = random.randint(1, FEE_SCALE // 2)
        out_fee = max(mid_fee, random.randint(1, FEE_SCALE // 2))
        pools.append(
            {
                "name": f"pool_{i:02d}",
                "A": str(random.randint(2, 10_000) * 10_000),
                "gamma": str(random.randint(10**11, 10**16)),
                "mid_fee": str(mid_fee),
                "out_fee": str(out_fee),
                "fee_gamma": str(random.randint(10**10, 10**18)),
                "adjustment_step_min": str(ADJUSTMENT_STEP_MIN),
                "adjustment_step_max": str(random.randint(*ADJUSTMENT_STEP_MAX_BPS) * WAD // BPS),
                "reserved_profit_fraction": str(random_fee_fraction(RESERVED_PROFIT_FRACTION_BPS)),
                "admin_fee": str(random_fee_fraction(ADMIN_FEE_BPS)),
                "policy": {"kind": policy_kind},
                "ma_time": str(1 + int(random.randint(60, 3_600) / math.log(2))),
                "initial_price": str(WAD),
                "initial_liquidity": [
                    str(jitter_bps(base_liquidity, INITIAL_LIQUIDITY_JITTER_BPS)),
                    str(jitter_bps(base_liquidity, INITIAL_LIQUIDITY_JITTER_BPS)),
                ],
            }
        )
    return pools


def exchange_action(state: SequenceState) -> Action | None:
    for coin_in in state.swap_directions():
        bounds = state.swap_bounds(coin_in)
        if bounds is None:
            continue
        dx = random_milli_wad((bounds[0] // MILLI_WAD, bounds[1] // MILLI_WAD))
        state.record_swap(coin_in, dx)
        return {"type": "exchange", "i": coin_in, "j": 1 - coin_in, "dx": str(dx)}
    return None


def regular_add_liquidity_action(state: SequenceState) -> Action:
    amounts = (random_milli_wad(REGULAR_ADD_MILLI), random_milli_wad(REGULAR_ADD_MILLI))
    state.record_regular_add(amounts)
    return {"type": "add_liquidity", "amounts": [str(amounts[0]), str(amounts[1])], "donation": False}


def donation_action(state: SequenceState) -> Action | None:
    for _ in range(DONATION_MAX_TRIES):
        amounts = (random_milli_wad(DONATION_MILLI), random_milli_wad(DONATION_MILLI))
        minted_estimate = amounts[0] + amounts[1]
        if state.can_accept_donation(minted_estimate):
            state.record_donation(amounts, minted_estimate)
            return {"type": "add_liquidity", "amounts": [str(amounts[0]), str(amounts[1])], "donation": True}
    return None


def remove_liquidity_action(state: SequenceState) -> Action | None:
    budget = state.remove_budget()
    if budget < MIN_REMOVE_LP:
        return None
    amount = min(max(MIN_REMOVE_LP, budget * random.randint(*REMOVE_BPS) // BPS), budget)
    state.record_remove(amount)
    return {"type": "remove_liquidity", "amount": str(amount), "min_amounts": ["0", "0"]}


def action_for_kind(kind: str, state: SequenceState) -> Action | None:
    actions = {
        "exchange": exchange_action,
        "add_liquidity": regular_add_liquidity_action,
        "donation": donation_action,
        "remove_liquidity": remove_liquidity_action,
    }
    try:
        return actions[kind](state)
    except KeyError as exc:
        raise ValueError(f"unknown action kind: {kind}") from exc


def random_action_kind() -> str:
    roll = random.random()
    for cutoff, kind in RANDOM_ACTION_MIX:
        if roll < cutoff:
            return kind
    return RANDOM_ACTION_MIX[-1][1]


def next_pool_action(step: int, state: SequenceState) -> Action:
    kind = FORCED_ACTION_KINDS[step] if step < len(FORCED_ACTION_KINDS) else random_action_kind()
    return action_for_kind(kind, state) or regular_add_liquidity_action(state)


def generate_action_sequences(
    trades_per_sequence: int = 20,
    start_ts: int = DEFAULT_START_TS,
    pool_configs: list[PoolConfig] | None = None,
    *,
    seed: int | None = None,
) -> list[dict[str, Any]]:
    if trades_per_sequence <= 0:
        raise ValueError("trades_per_sequence must be positive")
    previous_state = random.getstate()
    if seed is not None:
        random.seed(seed)
    try:
        actions: list[Action] = []
        timestamp = start_ts
        state = SequenceState.from_pools(pool_configs)
        for step in range(trades_per_sequence):
            if step % TIME_TRAVEL_EVERY_STEPS == 0:
                timestamp += random.randint(*TIME_TRAVEL_SECONDS)
                actions.append({"type": "time_travel", "timestamp": timestamp})
            actions.append(next_pool_action(step, state))
        return [{"name": "default", "start_timestamp": start_ts, "actions": actions}]
    finally:
        if seed is not None:
            random.setstate(previous_state)


def fixture_bundle(*, seed: int, num_pools: int = 3, trades: int = 64, policy: str = "none", start_ts: int = DEFAULT_START_TS) -> tuple[dict[str, Any], dict[str, Any]]:
    previous_state = random.getstate()
    random.seed(seed)
    try:
        pools = generate_pool_configs(num_pools=num_pools, policy_kind=policy)
        sequences = generate_action_sequences(trades, start_ts, pools)
    finally:
        random.setstate(previous_state)
    return {"seed": seed, "pools": pools}, {"seed": seed, "sequences": sequences}


def write_json(path: str | Path, payload: dict[str, Any]) -> None:
    output = Path(path).expanduser().resolve()
    if not output.parent.is_dir():
        raise FileNotFoundError(f"output directory does not exist: {output.parent}")
    output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def action_summary(actions: list[Action]) -> str:
    counts = Counter(action["type"] for action in actions)
    donations = sum(1 for action in actions if action["type"] == "add_liquidity" and action.get("donation"))
    return (
        f"time_travel={counts['time_travel']}, exchange={counts['exchange']}, "
        f"add_liquidity={counts['add_liquidity'] - donations}, donation={donations}, "
        f"remove_liquidity={counts['remove_liquidity']}"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pools", type=int, default=3)
    parser.add_argument("--trades", "--actions", dest="trades", type=int, default=64)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--start-ts", type=int, default=DEFAULT_START_TS)
    parser.add_argument("--out", required=True, type=Path, help="explicit output directory")
    parser.add_argument("--policy", choices=POLICY_KINDS, default="none")
    args = parser.parse_args(argv)
    args.out.mkdir(parents=True, exist_ok=True)
    pools, sequences = fixture_bundle(
        seed=args.seed,
        num_pools=args.pools,
        trades=args.trades,
        policy=args.policy,
        start_ts=args.start_ts,
    )
    write_json(args.out / "pools.json", pools)
    write_json(args.out / "sequences.json", sequences)
    (args.out / "seed.txt").write_text(f"{args.seed}\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
