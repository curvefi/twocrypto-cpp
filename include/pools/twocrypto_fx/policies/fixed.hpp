// Zero and fixed-fee policy implementations.
#pragma once

#include "common.hpp"

namespace arb {
namespace pools {
namespace twocrypto_fx {

template <typename T>
struct ZeroPolicy {
    struct State {};

    static T get_fee(
        const State& state,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config,
        const PolicyResearchContext<T>& research,
        const std::array<T, 2>& xp
    ) {
        (void)state;
        (void)params;
        (void)config;
        (void)research;
        (void)xp;
        return T(0);
    }

    static T get_price_scale(
        State& state,
        PolicyResearchContext<T>& research,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config
    ) {
        (void)state;
        (void)research;
        (void)params;
        (void)config;
        return T(0);
    }

    static void update_state(
        State& state,
        PolicyResearchContext<T>& research,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config,
        const PolicyUpdate<T>& update
    ) {
        (void)state;
        (void)research;
        (void)params;
        (void)config;
        (void)update;
    }
};

template <typename T>
struct FixedFeePolicy {
    using State = typename ZeroPolicy<T>::State;

    static T get_fee(
        const State& state,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config,
        const PolicyResearchContext<T>& research,
        const std::array<T, 2>& xp
    ) {
        (void)state;
        (void)config;
        (void)research;
        (void)xp;
        return params.fee;
    }

    static T get_price_scale(
        State& state,
        PolicyResearchContext<T>& research,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config
    ) {
        return ZeroPolicy<T>::get_price_scale(state, research, params, config);
    }

    static void update_state(
        State& state,
        PolicyResearchContext<T>& research,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config,
        const PolicyUpdate<T>& update
    ) {
        ZeroPolicy<T>::update_state(state, research, params, config, update);
    }
};

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
