"""Exact-integer reference model for an oracle-informed FXSwap fee policy."""

from __future__ import annotations

from dataclasses import dataclass


N_COINS = 2
PRECISION = 10**18
FEE_PRECISION = 10**10
A_MULTIPLIER = 10_000
MIN_FEE = FEE_PRECISION // 10 // 10_000
MAX_FEE = FEE_PRECISION


@dataclass(frozen=True)
class FeeDecision:
    fee: int
    reason: str
    direction: str | None = None
    p_old: int = 0
    p_new: int = 0
    value_in: int = 0
    value_out: int = 0
    edge_fee: int = 0
    fee_value: int = 0
    residual_value: int = 0


def newton_d(amp: int, xp: tuple[int, int]) -> int:
    """Mirror FXSwap StableswapMath.newton_D on the uint256 lattice."""
    if min(xp) <= 0 or max(xp) // min(xp) >= 10_000:
        raise ValueError("!balance")

    total = sum(xp)
    if total == 0:
        return 0

    d = total
    ann = amp * N_COINS
    for _ in range(255):
        d_p = d
        for x in xp:
            d_p = d_p * d // x
        d_p //= N_COINS**N_COINS

        d_prev = d
        numerator = (ann * total // A_MULTIPLIER + d_p * N_COINS) * d
        denominator = (
            (ann - A_MULTIPLIER) * d // A_MULTIPLIER
            + (N_COINS + 1) * d_p
        )
        d = numerator // denominator
        if abs(d - d_prev) <= 1:
            return d
    raise RuntimeError("newton_d did not converge")


def get_y(amp: int, xp: tuple[int, int], d: int, output_coin: int) -> int:
    """Mirror FXSwap StableswapMath.get_y for a proposed fee-free swap."""
    if output_coin not in (0, 1):
        raise ValueError("output_coin must be 0 or 1")

    ann = amp * N_COINS
    other = xp[1 - output_coin]
    c = d * d // (other * N_COINS)
    c = c * d * A_MULTIPLIER // (ann * N_COINS)
    b = other + d * A_MULTIPLIER // ann
    y = d
    for _ in range(255):
        y_prev = y
        y = (y * y + c) // (2 * y + b - d)
        if abs(y - y_prev) <= 1:
            return y
    raise RuntimeError("get_y did not converge")


def get_p(xp: tuple[int, int], d: int, amp: int) -> int:
    """Return the normalized marginal price, matching StableswapMath.get_p."""
    ann = amp * N_COINS
    dr = d // (N_COINS**N_COINS)
    for x in xp:
        dr = dr * d // x
    xp0_a = ann * xp[0] // A_MULTIPLIER
    return PRECISION * (xp0_a + dr * xp[0] // xp[1]) // (xp0_a + dr)


def external_price(xp: tuple[int, int], d: int, amp: int, price_scale: int) -> int:
    return get_p(xp, d, amp) * price_scale // PRECISION


def trial_swap_xp(
    xp_old: tuple[int, int],
    *,
    d: int,
    amp: int,
    input_coin: int,
    dx_xp: int,
) -> tuple[int, int]:
    """Construct the exact zero-fee xp endpoint passed to get_fee."""
    if input_coin not in (0, 1):
        raise ValueError("input_coin must be 0 or 1")
    if dx_xp <= 0:
        raise ValueError("dx_xp must be positive")

    output_coin = 1 - input_coin
    xp = list(xp_old)
    xp[input_coin] += dx_xp
    y = get_y(amp, (xp[0], xp[1]), d, output_coin)
    if y >= xp[output_coin]:
        raise ValueError("zero output")
    xp[output_coin] = y
    return xp[0], xp[1]


def quote_fair_fee(
    xp_old: tuple[int, int],
    xp_new: tuple[int, int],
    *,
    price_scale: int,
    d: int,
    amp: int,
    p_fair: int,
    base_fee: int,
    capture: int,
    rounding_unit_xp: int = 1,
) -> FeeDecision:
    """Evaluate the proposed Vyper fee policy with integer-floor arithmetic."""
    if not MIN_FEE <= base_fee <= MAX_FEE:
        raise ValueError("base_fee is outside the pool fee range")
    if not 0 <= capture <= PRECISION:
        raise ValueError("capture must be a WAD fraction")
    if rounding_unit_xp < 0:
        raise ValueError("rounding_unit_xp must be non-negative")
    if min((*xp_old, *xp_new, price_scale, d, amp, p_fair)) <= 0:
        return FeeDecision(MAX_FEE, "uninitialized")

    is_1_to_0 = False
    if xp_new[1] > xp_old[1] and xp_new[0] < xp_old[0]:
        is_1_to_0 = True
        direction = "1_to_0"
        dx_xp = xp_new[1] - xp_old[1]
        dy_xp = xp_old[0] - xp_new[0]
    elif xp_new[0] > xp_old[0] and xp_new[1] < xp_old[1]:
        direction = "0_to_1"
        dx_xp = xp_new[0] - xp_old[0]
        dy_xp = xp_old[1] - xp_new[1]
    else:
        return FeeDecision(0, "native_fallback")

    if dy_xp <= rounding_unit_xp:
        return FeeDecision(MAX_FEE, "dust", direction=direction)
    dy_xp -= rounding_unit_xp

    p_old = external_price(xp_old, d, amp, price_scale)
    p_new = external_price(xp_new, d, amp, price_scale)
    if is_1_to_0:
        admitted = p_old > p_fair and p_new < p_old and p_new >= p_fair
    else:
        admitted = p_old < p_fair and p_new > p_old and p_new <= p_fair
    if not admitted:
        return FeeDecision(
            MAX_FEE,
            "wrong_way_or_crossed_fair",
            direction=direction,
            p_old=p_old,
            p_new=p_new,
        )

    coin1_xp = dx_xp if is_1_to_0 else dy_xp
    coin1_amount = coin1_xp * PRECISION // price_scale
    if is_1_to_0:
        value_in = coin1_amount * p_fair // PRECISION
        value_out = dy_xp
    else:
        value_in = dx_xp
        value_out = coin1_amount * p_fair // PRECISION

    if value_out <= value_in:
        return FeeDecision(
            MAX_FEE,
            "no_gross_edge",
            direction=direction,
            p_old=p_old,
            p_new=p_new,
            value_in=value_in,
            value_out=value_out,
        )

    edge_fee = (value_out - value_in) * FEE_PRECISION // value_out
    fee = base_fee
    if edge_fee > base_fee:
        fee += (edge_fee - base_fee) * capture // PRECISION
    fee = min(fee, MAX_FEE)

    fee_value = value_out * fee // FEE_PRECISION
    residual_value = value_out - fee_value - value_in
    return FeeDecision(
        fee,
        "admitted",
        direction=direction,
        p_old=p_old,
        p_new=p_new,
        value_in=value_in,
        value_out=value_out,
        edge_fee=edge_fee,
        fee_value=fee_value,
        residual_value=residual_value,
    )


def find_trial_xp(
    xp_old: tuple[int, int],
    *,
    d: int,
    amp: int,
    price_scale: int,
    target_price: int,
) -> tuple[int, int]:
    """Find an invariant-consistent trial endpoint near a requested marginal price."""
    p_old = external_price(xp_old, d, amp, price_scale)
    if target_price == p_old:
        return xp_old
    input_coin = 0 if target_price > p_old else 1

    low = 1
    high = max(xp_old[input_coin] // 1_000_000, 1)
    while True:
        trial = trial_swap_xp(xp_old, d=d, amp=amp, input_coin=input_coin, dx_xp=high)
        price = external_price(trial, d, amp, price_scale)
        reached = price >= target_price if input_coin == 0 else price <= target_price
        if reached:
            break
        high *= 2
        if high > xp_old[input_coin] * 100:
            raise ValueError("target price is outside the searchable range")

    for _ in range(256):
        if low >= high:
            break
        mid = (low + high) // 2
        trial = trial_swap_xp(xp_old, d=d, amp=amp, input_coin=input_coin, dx_xp=mid)
        price = external_price(trial, d, amp, price_scale)
        reached = price >= target_price if input_coin == 0 else price <= target_price
        if reached:
            high = mid
        else:
            low = mid + 1
    return trial_swap_xp(xp_old, d=d, amp=amp, input_coin=input_coin, dx_xp=high)
