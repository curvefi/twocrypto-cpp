// Native twocrypto fee and price-scale policy implementation.
#pragma once

#include <cmath>
#include <type_traits>

#include "common.hpp"

namespace arb {
namespace pools {
namespace twocrypto_fx {

template <typename T>
struct NativeFeePolicy {
    struct State {
        PolicyPoolSnapshot<T> pool{};
    };

    static T get_fee(
        const State& state,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config,
        const PolicyResearchContext<T>& research,
        const std::array<T, 2>& xp
    ) {
        (void)params;
        (void)state;
        (void)research;

        // Mirrors vy _fee: fee_gamma == 0 yields out_fee via the formula.
        T bsum = xp[0] + xp[1];
        if (bsum == T(0)) {
            return config.out_fee;  // formula limit; vy reverts on empty pool
        }

        T balance_term = config.precision * T(4) * xp[0] / bsum * xp[1] / bsum;
        balance_term = config.fee_gamma * balance_term /
            (config.fee_gamma * balance_term / config.precision + config.precision - balance_term);

        return (
            config.mid_fee * balance_term +
            config.out_fee * (config.precision - balance_term)
        ) / config.precision;
    }

    static T get_price_scale(
        State& state,
        PolicyResearchContext<T>& research,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config
    ) {
        (void)params;

        const auto& s = state.pool;
        if (s.ts == 0 || s.price_scale == T(0)) {
            return T(0);
        }

        T price_scale = s.price_scale;
        T price_oracle = s.price_oracle;
        if (s.ts < research.block_timestamp) {
            T dt = T(research.block_timestamp - s.ts);
            T ema_input = clamp_policy_value(s.last_prices, price_scale / 2, price_scale * 2);
            if constexpr (std::is_same_v<T, uint256>) {
                auto neg = int256(
                    -(
                        int256(dt) *
                        int256(config.precision) /
                        int256(config.ma_time)
                    )
                );
                T alpha = MathOps<T>::wad_exp(neg);
                price_oracle = (
                    ema_input * (config.precision - alpha) + price_oracle * alpha
                ) / config.precision;
            } else {
                auto alpha = std::exp(
                    -static_cast<double>(dt) / static_cast<double>(config.ma_time)
                );
                price_oracle = ema_input * (T(1) - T(alpha)) + price_oracle * T(alpha);
            }
        }
        return price_oracle;
    }

    static void update_state(
        State& state,
        PolicyResearchContext<T>& research,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config,
        const PolicyUpdate<T>& update
    ) {
        (void)research;
        (void)params;
        (void)config;
        store_policy_snapshot(state.pool, update);
    }
};

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
