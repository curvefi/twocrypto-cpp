// Swap-local fair-price reports; the pool's native EMA drives price_scale.
#pragma once

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "pools/twocrypto_fx/policy_descriptor.hpp"
#include "pools/twocrypto_fx/policy_types.hpp"

namespace arb::pools::twocrypto_fx {

template <typename T>
struct ChallengeFeePolicy {
    inline static constexpr PolicyDescriptor<3> DESCRIPTOR{
        "reporting_fair_fee", {{
            {"base_fee", 0, "relative", 0.01L, 0.0L, 1.0L, 0.0001L},
            {"capture", 1, "relative", 0.5L, 0.0L, 1.0L, 0.01L},
            {"fallback_fee", 2, "relative", 0.01L, 0.0L, 1.0L, 0.0001L},
        }},
    };
    static constexpr std::size_t PARAM_COUNT = DESCRIPTOR.size();
    static constexpr const char* NAME = DESCRIPTOR.name.data();
    static constexpr bool USES_NATIVE_FEE = false;
    static constexpr bool USES_SWAP_REPORTS = true;

    struct State {
        std::array<T, 2> xp{T(0), T(0)};
        T price_scale{T(0)};
    };

    static void validate_params(const PolicyConfig<T>& params,
                                const PolicyPoolConfig<T>& config) {
        if (params.n_params != PARAM_COUNT)
            throw std::invalid_argument("reporting fair fee requires 3 parameters");
        for (std::size_t i = 0; i < PARAM_COUNT; ++i) {
            const T limit = i == 1 ? config.precision : config.fee_precision;
            const T value = params.params[i];
            if (!(value >= T(0) && value <= limit))
                throw std::invalid_argument("reporting fair fee parameter out of range");
            if constexpr (std::is_floating_point_v<T>) {
                if (!std::isfinite(value))
                    throw std::invalid_argument("reporting fair fee parameter must be finite");
            }
        }
    }

    static T get_fee(const State& state, const PolicyConfig<T>& params,
                     const PolicyPoolConfig<T>& config,
                     const PolicyResearchContext<T>& research,
                     const std::array<T, 2>& xp_new) {
        // Zero is the native-fee sentinel. Return at least the pool's existing
        // 0.1 bp noise floor so a zero configured fee does not select native fees.
        const T minimum_fee = config.fee_precision / T(100000);
        const T fallback = std::max(minimum_fee, params.params[2]);
        const bool sell_coin1 = xp_new[1] > state.xp[1] && xp_new[0] < state.xp[0];
        const bool sell_coin0 = xp_new[0] > state.xp[0] && xp_new[1] < state.xp[1];
        if ((!sell_coin0 && !sell_coin1) || !(state.price_scale > T(0)) ||
            !(research.price_feed > T(0)) ||
            research.price_feed_timestamp != research.block_timestamp)
            return fallback;

        const T dx = sell_coin1 ? xp_new[1] - state.xp[1] : xp_new[0] - state.xp[0];
        T dy = sell_coin1 ? state.xp[0] - xp_new[0] : state.xp[1] - xp_new[1];
        if constexpr (std::is_same_v<T, uint256>) {
            if (dy <= T(1)) return fallback;
            dy -= T(1);
        }
        // Mark both pre-fee swap legs in coin0. Edge is surplus/value_out,
        // matching the output-denominated fee charged by the pool.
        const T coin1 = (sell_coin1 ? dx : dy) * config.precision / state.price_scale;
        const T value_in = sell_coin1 ? coin1 * research.price_feed / config.precision : dx;
        const T value_out = sell_coin1 ? dy : coin1 * research.price_feed / config.precision;
        const T edge = value_out > value_in
            ? (value_out - value_in) * config.fee_precision / value_out : T(0);
        const T fee = std::max(params.params[0], T(edge * params.params[1] / config.precision));
        return std::min(fallback, std::max(minimum_fee, fee));
    }

    static T fee_floor(const PolicyConfig<T>& params, const PolicyPoolConfig<T>& config, const T&) {
        // Both report outcomes charge at least this much. Let sizing skip
        // opportunities that cannot cover even the cheapest possible fee.
        return std::max(T(config.fee_precision / T(100000)),
                        std::min(params.params[0], params.params[2]));
    }

    static T context_fee_floor(const State&, const PolicyConfig<T>& params,
                              const PolicyPoolConfig<T>& config,
                              const PolicyResearchContext<T>& research, std::size_t) {
        if (!(research.price_feed > T(0)) ||
            research.price_feed_timestamp != research.block_timestamp)
            return std::max(T(config.fee_precision / T(100000)), params.params[2]);
        return fee_floor(params, config, T(0));
    }

    static T get_price_scale(State&, PolicyResearchContext<T>&,
                             const PolicyConfig<T>&, const PolicyPoolConfig<T>&) {
        return T(0);
    }

    static void update_state(State& state, PolicyResearchContext<T>&,
                            const PolicyConfig<T>&, const PolicyPoolConfig<T>&,
                            const PolicyUpdate<T>& update) {
        state.xp = update.xp;
        state.price_scale = update.price_scale;
    }
};

} // namespace arb::pools::twocrypto_fx
