// Experimental endogenous fee surface. A proposed swap reveals a fair-price
// boundary from its virtual post-fee marginal price and an assumed external
// arbitrage cost; no external price feed is consumed by the policy.
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
    inline static constexpr PolicyDescriptor<3> DESCRIPTOR{
        "implied_fair_fee",
        {{
            {"base_fee", 0, "relative", 0.01L, 0.00001L, 1.0L, 0.0001L},
            {"capture", 1, "relative", 0.5L, 0.0L, 1.0L, 0.01L},
            {"assumed_arb_cost", 2, "relative", 0.0002L, 0.0L, 0.1L, 0.0001L},
        }},
    };
    static constexpr std::size_t PARAM_COUNT = DESCRIPTOR.size();
    static constexpr const char* NAME = DESCRIPTOR.name.data();
    static constexpr bool USES_NATIVE_FEE = false;
    static constexpr std::size_t FEE_SOLVER_STEPS = 8;

    struct State {
        std::array<T, 2> xp{T(0), T(0)};
        T price_scale{T(0)};
        T D{T(0)};
    };

    static T base_fee(const PolicyConfig<T>& params) { return params.params[0]; }
    static T capture(const PolicyConfig<T>& params) { return params.params[1]; }
    static T arb_cost(const PolicyConfig<T>& params) { return params.params[2]; }

    static void validate_params(
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config
    ) {
        if (params.n_params != PARAM_COUNT) {
            throw std::invalid_argument("implied-fair fee policy requires 3 parameters");
        }
        const T fee = base_fee(params);
        const T share = capture(params);
        const T cost = arb_cost(params);
        if (!(config.precision > T(0)) || !(config.fee_precision > T(0))) {
            throw std::invalid_argument("implied-fair fee policy requires positive precisions");
        }
        if (!(fee > T(0)) || fee > config.fee_precision) {
            throw std::invalid_argument("base fee");
        }
        if (share < T(0) || share > config.precision) {
            throw std::invalid_argument("capture");
        }
        if (cost < T(0) || cost >= config.precision) {
            throw std::invalid_argument("assumed arb cost");
        }
        if constexpr (std::is_floating_point_v<T>) {
            if (!std::isfinite(fee) || !std::isfinite(share) || !std::isfinite(cost)) {
                throw std::invalid_argument("implied-fair fee parameters must be finite");
            }
        }
    }

    static T get_fee(
        const State& state,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config,
        const PolicyResearchContext<T>&,
        const std::array<T, 2>& xp_new
    ) {
        const auto& xp_old = state.xp;
        const bool is_1_to_0 =
            xp_new[1] > xp_old[1] && xp_new[0] < xp_old[0];
        const bool is_0_to_1 =
            xp_new[0] > xp_old[0] && xp_new[1] < xp_old[1];
        if (!is_1_to_0 && !is_0_to_1) return T(0);

        const T max_fee = config.fee_precision;
        if (!(state.price_scale > T(0)) || !(state.D > T(0)) ||
            !(xp_old[0] > T(0)) || !(xp_old[1] > T(0)) ||
            !(xp_new[0] > T(0)) || !(xp_new[1] > T(0))) {
            return max_fee;
        }

        T dx_xp = T(0);
        T dy_xp = T(0);
        std::size_t output_coin = 0;
        if (is_1_to_0) {
            dx_xp = xp_new[1] - xp_old[1];
            dy_xp = xp_old[0] - xp_new[0];
            output_coin = 0;
        } else {
            dx_xp = xp_new[0] - xp_old[0];
            dy_xp = xp_old[1] - xp_new[1];
            output_coin = 1;
        }

        T rounding_unit = T(0);
        if constexpr (std::is_same_v<T, uint256>) rounding_unit = T(1);
        if (!(dy_xp > rounding_unit)) return max_fee;
        dy_xp -= rounding_unit;

        const T p_old = MathOps<T>::get_p(
            xp_old, state.D, {config.A, config.gamma}
        ) * state.price_scale / config.precision;
        const T p_trial = MathOps<T>::get_p(
            xp_new, state.D, {config.A, config.gamma}
        ) * state.price_scale / config.precision;
        if ((is_1_to_0 && !(p_trial < p_old)) ||
            (is_0_to_1 && !(p_trial > p_old))) {
            return max_fee;
        }

        T fee = base_fee(params);
        const T cost = arb_cost(params);
        const T share = capture(params);
        for (std::size_t step = 0; step < FEE_SOLVER_STEPS; ++step) {
            auto xp_post = xp_new;
            xp_post[output_coin] += dy_xp * fee / config.fee_precision;
            const T d_post = MathOps<T>::newton_D(
                config.A, config.gamma, xp_post, state.D
            );
            const T p_post = MathOps<T>::get_p(
                xp_post, d_post, {config.A, config.gamma}
            ) * state.price_scale / config.precision;

            T fair = T(0);
            if (is_1_to_0) {
                fair = p_post * (config.fee_precision - fee) /
                    config.fee_precision;
                fair = fair * config.precision / (config.precision + cost);
            } else {
                if (!(fee < config.fee_precision) || !(cost < config.precision)) {
                    return max_fee;
                }
                fair = p_post * config.fee_precision /
                    (config.fee_precision - fee);
                fair = fair * config.precision / (config.precision - cost);
            }
            if (!(fair > T(0))) return max_fee;

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

            T target = base_fee(params);
            if (value_out > value_in) {
                const T edge =
                    (value_out - value_in) * config.fee_precision / value_out;
                if (edge > target) {
                    target += (edge - target) * share / config.precision;
                }
            }
            if (target > max_fee) target = max_fee;
            fee = (fee + target) / T(2);
        }
        return fee < max_fee ? fee : max_fee;
    }

    static T fee_floor(
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>&,
        const T&
    ) {
        return base_fee(params);
    }

    static T get_price_scale(
        State&,
        PolicyResearchContext<T>&,
        const PolicyConfig<T>&,
        const PolicyPoolConfig<T>&
    ) {
        return T(0);
    }

    static void update_state(
        State& state,
        PolicyResearchContext<T>&,
        const PolicyConfig<T>&,
        const PolicyPoolConfig<T>&,
        const PolicyUpdate<T>& update
    ) {
        state.xp = update.xp;
        state.price_scale = update.price_scale;
        state.D = update.D;
    }
};

} // namespace arb::pools::twocrypto_fx
