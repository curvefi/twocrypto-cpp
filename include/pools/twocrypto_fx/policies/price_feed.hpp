// Pass a harness-supplied generic price feed through to Twocrypto's
// native price-scale actuator. The pool retains its normal step limits,
// donation/profit gates, invariant checks, and rollback semantics.
#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>

#include "pools/twocrypto_fx/policy_descriptor.hpp"
#include "pools/twocrypto_fx/policy_types.hpp"

namespace arb::pools::twocrypto_fx {

template <typename T>
struct ChallengeFeePolicy {
    inline static constexpr PolicyDescriptor<0> DESCRIPTOR{
        "price_feed_passthrough",
        {},
    };
    static constexpr std::size_t PARAM_COUNT = DESCRIPTOR.size();
    static constexpr const char* NAME = DESCRIPTOR.name.data();
    static constexpr bool USES_NATIVE_FEE = true;

    struct State {
        T price_scale{T(0)};
    };

    static void validate_params(
        const PolicyConfig<T>& params, const PolicyPoolConfig<T>& = {}
    ) {
        if (params.n_params != PARAM_COUNT) {
            throw std::invalid_argument("price-feed policy takes no parameters");
        }
    }

    static T get_fee(
        const State&,
        const PolicyConfig<T>&,
        const PolicyPoolConfig<T>&,
        const PolicyResearchContext<T>&,
        const std::array<T, 2>&
    ) {
        return T(0);
    }

    static T fee_floor(
        const PolicyConfig<T>&,
        const PolicyPoolConfig<T>&,
        const T& native_floor
    ) {
        return native_floor;
    }

    static T get_price_scale(
        State& state,
        PolicyResearchContext<T>& research,
        const PolicyConfig<T>&,
        const PolicyPoolConfig<T>&
    ) {
        if (!(research.price_feed > T(0))) {
            throw std::runtime_error("price-feed policy requires an attached feed");
        }
        if (research.price_feed_timestamp > research.block_timestamp) {
            throw std::runtime_error("price-feed policy received a future value");
        }
        if (!(state.price_scale > T(0))) return research.price_feed;
        if (research.price_feed >= state.price_scale) {
            return state.price_scale + T(5) * (research.price_feed - state.price_scale);
        }
        const T gap = state.price_scale - research.price_feed;
        return gap < state.price_scale / T(5)
            ? state.price_scale - T(5) * gap
            : state.price_scale / T(5);
    }

    static void update_state(
        State& state,
        PolicyResearchContext<T>&,
        const PolicyConfig<T>&,
        const PolicyPoolConfig<T>&,
        const PolicyUpdate<T>& update
    ) {
        state.price_scale = update.price_scale;
    }
};

} // namespace arb::pools::twocrypto_fx
