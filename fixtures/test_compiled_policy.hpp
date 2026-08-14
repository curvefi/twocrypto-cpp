// Minimal deterministic compiled policy fixture for testing compiled policy extension point.
#pragma once

#include <array>
#include "pools/twocrypto_fx/policies/common.hpp"

namespace arb {
namespace pools {
namespace twocrypto_fx {

template <typename T>
struct CompiledPolicy {
    static constexpr const char* NAME = "test_fixture_policy";

    struct State {
        uint64_t step_count{0};
    };

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
        if (params.fee != T(0)) {
            return params.fee;
        }
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
        (void)config;
        if (params.n_params > 0) {
            return params.params[0];
        }
        return T(0);
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
        (void)update;
        state.step_count += 1;
    }

    static T fee_floor(
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config,
        const T& native_floor
    ) {
        (void)config;
        if (params.fee != T(0)) {
            return params.fee;
        }
        return native_floor;
    }
};

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
