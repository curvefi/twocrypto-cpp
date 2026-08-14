// Default compiled policy: native passthrough.
//
// This is the canonical fixed-policy interface. A repository-owned policy
// header defines the same `ChallengeFeePolicy<T>` struct and static methods.
// Returning zero from get_fee and get_price_scale delegates to the native
// fee surface and native price-scale target.
#pragma once

#include <array>

#include "common.hpp"

namespace arb {
namespace pools {
namespace twocrypto_fx {

template <typename T>
struct ChallengeFeePolicy {
    static constexpr std::size_t PARAM_COUNT = 0;

    static constexpr const char* NAME = "native_passthrough";

    // Value-semantic per-pool state; copied by pool transaction snapshots.
    struct State {};

    // Return a fee fraction, or zero to use the native fee.
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

    // Return an absolute price-scale target, or zero for the native target.
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

    // Observe each committed state transition.
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

    // Conservative lower bound on every possible fee returned above.
    static T fee_floor(
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config,
        const T& native_floor
    ) {
        (void)params;
        (void)config;
        return native_floor;
    }
};

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
