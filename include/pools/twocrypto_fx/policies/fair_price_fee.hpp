// Oracle-informed FXSwap fee surface, written as a direct Vyper-portable model.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <type_traits>

#include "pools/twocrypto_fx/policy_descriptor.hpp"
#include "pools/twocrypto_fx/policy_types.hpp"

namespace arb::pools::twocrypto_fx {

template <typename T>
struct ChallengeFeePolicy {
    inline static constexpr PolicyDescriptor<2> DESCRIPTOR{
        "fair_price_fee",
        {{
            {"base_fee", 0, "relative", 0.01L, 0.00001L, 1.0L, 0.0001L},
            {"capture", 1, "relative", 0.5L, 0.0L, 1.0L, 0.01L},
        }},
    };
    static constexpr std::size_t PARAM_COUNT = DESCRIPTOR.size();
    static constexpr const char* NAME = DESCRIPTOR.name.data();
    static constexpr bool USES_NATIVE_FEE = false;

    struct State {
        std::array<T, 2> xp{T(0), T(0)};
        T price_scale{T(0)};
        T D{T(0)};
    };

    static T param(const PolicyConfig<T>& params, std::size_t index) {
        if (params.n_params != PARAM_COUNT) {
            throw std::invalid_argument("fair-price fee policy requires 2 parameters");
        }
        return params.params[index];
    }

    static T base_fee(const PolicyConfig<T>& params) {
        return param(params, 0);
    }

    static T capture(const PolicyConfig<T>& params) {
        return param(params, 1);
    }

    static void validate_params(
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config
    ) {
        const T fee = base_fee(params);
        const T share = capture(params);
        if (!(config.precision > T(0)) || !(config.fee_precision > T(0))) {
            throw std::invalid_argument("fair-price fee policy requires positive precisions");
        }
        if (!(fee > T(0)) || fee > config.fee_precision) {
            throw std::invalid_argument("base fee");
        }
        if (share < T(0) || share > config.precision) {
            throw std::invalid_argument("capture");
        }
        if constexpr (std::is_floating_point_v<T>) {
            if (!std::isfinite(fee) || !std::isfinite(share)) {
                throw std::invalid_argument("fair-price fee policy parameters must be finite");
            }
        }
    }

    // Exact local translation of FXSwap StableswapMath.get_p. Gamma is absent
    // because it is a compatibility no-op in the FXSwap StableSwap invariant.
    static T marginal_price(
        const std::array<T, 2>& xp,
        const T& d_value,
        const T& amp,
        const T& price_scale,
        const T& precision
    ) {
        const T ann = amp * T(2);
        T dr = d_value / T(4);
        dr = dr * d_value / xp[0];
        dr = dr * d_value / xp[1];
        const T xp0_a = ann * xp[0] / T(10000);
        const T normalized = precision * (
            xp0_a + dr * xp[0] / xp[1]
        ) / (xp0_a + dr);
        return normalized * price_scale / precision;
    }

    static T get_fee(
        const State& state,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config,
        const PolicyResearchContext<T>& research,
        const std::array<T, 2>& xp_new
    ) {
        validate_params(params, config);

        const auto& xp_old = state.xp;
        const bool is_1_to_0 =
            xp_new[1] > xp_old[1] && xp_new[0] < xp_old[0];
        const bool is_0_to_1 =
            xp_new[0] > xp_old[0] && xp_new[1] < xp_old[1];

        // Liquidity operations and current-state fee queries use the native
        // fee. This branch also covers initial liquidity before policy state
        // has been seeded by the pool.
        if (!is_1_to_0 && !is_0_to_1) return T(0);

        const T max_fee = config.fee_precision;
        if (!(state.price_scale > T(0)) || !(state.D > T(0)) ||
            !(xp_old[0] > T(0)) || !(xp_old[1] > T(0)) ||
            !(xp_new[0] > T(0)) || !(xp_new[1] > T(0)) ||
            !(research.price_feed > T(0)) ||
            research.price_feed_timestamp != research.block_timestamp) {
            return max_fee;
        }

        T dx_xp = T(0);
        T dy_xp = T(0);
        if (is_1_to_0) {
            dx_xp = xp_new[1] - xp_old[1];
            dy_xp = xp_old[0] - xp_new[0];
        } else {
            dx_xp = xp_new[0] - xp_old[0];
            dy_xp = xp_old[1] - xp_new[1];
        }

        T rounding_unit = T(0);
        if constexpr (std::is_same_v<T, uint256>) rounding_unit = T(1);
        if (!(dy_xp > rounding_unit)) return max_fee;
        dy_xp -= rounding_unit;

        const T p_old = marginal_price(
            xp_old, state.D, config.A, state.price_scale, config.precision
        );
        const T p_new = marginal_price(
            xp_new, state.D, config.A, state.price_scale, config.precision
        );
        const T fair = research.price_feed;

        // The zero-fee endpoint is the furthest the trade can move reserves.
        // Requiring it to remain on the current side of fair is therefore a
        // conservative no-crossing guard for the retained-fee endpoint.
        if (is_1_to_0) {
            if (!(p_old > fair && p_new < p_old && p_new >= fair)) {
                return max_fee;
            }
        } else {
            if (!(p_old < fair && p_new > p_old && p_new <= fair)) {
                return max_fee;
            }
        }

        // xp[1] is native coin1 amount multiplied by price_scale. Remove that
        // pool coordinate before marking the leg at the fresh fair price.
        const T coin1_xp = is_1_to_0 ? dx_xp : dy_xp;
        const T coin1_amount =
            coin1_xp * config.precision / state.price_scale;

        T value_in = T(0);
        T value_out = T(0);
        if (is_1_to_0) {
            value_in = coin1_amount * fair / config.precision;
            value_out = dy_xp;
        } else {
            value_in = dx_xp;
            value_out = coin1_amount * fair / config.precision;
        }
        if (!(value_out > value_in)) return max_fee;

        // Floor the available surplus directly. The algebraically equivalent
        // `fee_precision - value_in * fee_precision / value_out` rounds up by
        // one fee unit on the uint lattice and can overcharge at full capture.
        const T edge_fee =
            (value_out - value_in) * config.fee_precision / value_out;
        T fee = base_fee(params);
        if (edge_fee > fee) {
            fee += (edge_fee - fee) * capture(params) / config.precision;
        }
        return fee < max_fee ? fee : max_fee;
    }

    static T fee_floor(
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config,
        const T&
    ) {
        validate_params(params, config);
        return base_fee(params);
    }

    static T get_price_scale(
        State&,
        PolicyResearchContext<T>&,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config
    ) {
        validate_params(params, config);
        return T(0);
    }

    static void update_state(
        State& state,
        PolicyResearchContext<T>&,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config,
        const PolicyUpdate<T>& update
    ) {
        validate_params(params, config);
        state.xp = update.xp;
        state.price_scale = update.price_scale;
        state.D = update.D;
    }
};

} // namespace arb::pools::twocrypto_fx
