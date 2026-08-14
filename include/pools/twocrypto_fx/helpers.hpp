// Helper utilities for twocrypto_fx pool
#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include "pools/twocrypto_fx/stableswap_math.hpp"
#include "pools/twocrypto_fx/twocrypto.hpp"

namespace arb {
namespace pools {
namespace twocrypto_fx {

template <typename T>
using PoolT = TwoCryptoPool<T>;

// -----------------------------------------------------------------------------
// Balance/vector helpers
// -----------------------------------------------------------------------------
template <typename T>
inline std::array<T, 2> pool_xp_from(
    const PoolT<T>& pool,
    const std::array<T, 2>& balances,
    const T& price_scale
) {
    return {
        balances[0] * pool.precisions[0],
        balances[1] * pool.precisions[1] * price_scale / PoolTraits<T>::PRECISION()
    };
}

template <typename T>
inline std::array<T, 2> pool_xp_current(const PoolT<T>& pool) {
    return pool_xp_from(pool, pool.balances, pool.cached_price_scale);
}

template <typename T>
inline T xp_to_tokens_j(const PoolT<T>& pool, size_t j, T amount_xp, const T& price_scale) {
    T v = amount_xp;
    if (j == 1) {
        v = v * PoolTraits<T>::PRECISION() / price_scale;
    }
    return v / pool.precisions[j];
}

template <typename T>
inline T balance_indicator(const PoolT<T>& pool) {
    const T ps = pool.cached_price_scale;
    const auto xp = pool_xp_from(pool, pool.balances, ps);
    const T denom = xp[0] + xp[1];
    if (!(denom > T(0))) {
        return T(0);
    }
    return static_cast<T>(4) * xp[0] * xp[1] / (denom * denom);
}

// -----------------------------------------------------------------------------
// Simulation helper (no state change)
// -----------------------------------------------------------------------------

// Bumped post-swap xp for an i->j swap of dx (no state change): returns the xp
// vector after removing the output leg, plus the consumed amount in xp units.
template <typename T>
inline std::pair<std::array<T, 2>, T> post_swap_xp(
    const PoolT<T>& pool,
    size_t i,
    size_t j,
    T dx,
    const T& price_scale
) {
    std::array<T, 2> xp = pool_xp_from(
        pool,
        {pool.balances[0] + (i == 0 ? dx : T(0)),
         pool.balances[1] + (i == 1 ? dx : T(0))},
        price_scale);
    const T y_out = MathOps<T>::get_y_unchecked(pool.A, pool.gamma, xp, pool.D, j);
    const T dy_xp = xp[j] - y_out;
    xp[j] -= dy_xp;
    return {xp, dy_xp};
}

template <typename T>
inline std::pair<T, T> simulate_exchange_once(
    const PoolT<T>& pool,
    size_t i,
    size_t j,
    T dx
) {
    const T ps = pool.cached_price_scale;
    auto [xp, dy_xp] = post_swap_xp(pool, i, j, dx, ps);

    T dy_tokens  = xp_to_tokens_j(pool, j, dy_xp, ps);
    T fee_pool   = pool.fee(xp);
    T fee_tokens = fee_pool * dy_tokens / PoolTraits<T>::FEE_PRECISION();
    return {dy_tokens - fee_tokens, fee_tokens};
}

// Floating-only: relative price-impact rate lambda = -d ln(m)/d(dx) >= 0 of
// the marginal output m for an i->j swap, evaluated at dx = 0 (per token of
// input coin). Closed form on the n=2 invariant (x + y + F - D)*x*y = G with
// F = D*A_MULT/Ann: m = y(x+S)/(x(y+S)) with S = x+y+F-D, so
//   lambda_xp = 1/x + (1-2m)/ (y+S) + m/y - (2-m)/(x+S).
// Used to seed the trade-size search at dx ~ edge/lambda; needs no fee model
// and no root solve.
template <typename T>
inline T price_impact_rate(const PoolT<T>& pool, size_t i, size_t j) {
    static_assert(std::is_floating_point_v<T>, "price_impact_rate is floating-only");
    const T ps = pool.cached_price_scale;
    const auto xp = pool_xp_from(pool, pool.balances, ps);
    const T x = xp[i];
    const T y = xp[j];
    const T D = pool.D;
    if (!(x > T(0)) || !(y > T(0)) || !(D > T(0))) {
        return T(0);
    }
    const T Ann = pool.A * T(2);
    const T F = D * MathTraits<T>::A_MULTIPLIER() / Ann;
    const T S = x + y + F - D;
    const T u = x + S;
    const T v = y + S;
    if (!(S > T(0)) || !(u > T(0)) || !(v > T(0))) {
        return T(0);
    }
    const T m0 = y * u / (x * v);
    T lambda = T(1) / x + (T(1) - T(2) * m0) / v + m0 / y - (T(2) - m0) / u;
    const T dxp_in_ddx = (i == 0)
        ? pool.precisions[0]
        : pool.precisions[1] * ps / PoolTraits<T>::PRECISION();
    lambda *= dxp_in_ddx;
    return lambda > T(0) ? lambda : T(0);
}

template <typename T>
inline T quoted_exchange_fee_fraction(
    const PoolT<T>& pool,
    size_t i,
    size_t j,
    T dx
) {
    if (!(dx > T(0))) {
        return pool.fee(pool_xp_current(pool));
    }

    const T ps = pool.cached_price_scale;
    auto [xp, dy_xp] = post_swap_xp(pool, i, j, dx, ps);
    (void)dy_xp;

    return pool.fee(xp);
}

template <typename T>
inline T viewer_exchange_fee_fraction(const PoolT<T>& pool, T cex_price) {
    const T min_dx0 = pool.balances[0] / T(1000000);
    const T min_dx1 = pool.balances[1] / T(1000000);

    if (cex_price > T(0)) {
        const auto xp_now = pool_xp_current(pool);
        const T p_pool = MathOps<T>::get_p(
            xp_now,
            pool.D,
            {pool.A, pool.gamma}
        ) * pool.cached_price_scale;
        if (cex_price >= p_pool && min_dx0 > T(0)) {
            return quoted_exchange_fee_fraction(pool, 0, 1, min_dx0);
        }
        if (min_dx1 > T(0)) {
            return quoted_exchange_fee_fraction(pool, 1, 0, min_dx1);
        }
    }

    T sum = T(0);
    T count = T(0);
    if (min_dx0 > T(0)) {
        sum += quoted_exchange_fee_fraction(pool, 0, 1, min_dx0);
        count += T(1);
    }
    if (min_dx1 > T(0)) {
        sum += quoted_exchange_fee_fraction(pool, 1, 0, min_dx1);
        count += T(1);
    }
    if (count > T(0)) {
        return sum / count;
    }
    return pool.fee(pool_xp_current(pool));
}

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
