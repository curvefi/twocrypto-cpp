// Experimental truth-revealing fee surface for an arb-only market.
//
// The proposed trade size reveals the arbitrageur's external fair price from
// the marginal derivative of the complete net-output schedule. Successive
// rounds fold the preceding round's size-dependent fee into that derivative,
// avoiding the missing df/dq term in a raw post-trade get_p inversion.
#pragma once

#include <algorithm>
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
    inline static constexpr PolicyDescriptor<4> DESCRIPTOR{
        "revealed_fair_fee",
        {{
            {"base_fee", 0, "relative", 0.01L, 0.00001L, 1.0L, 0.0001L},
            {"capture", 1, "relative", 0.5L, 0.0L, 1.0L, 0.01L},
            {"assumed_arb_cost", 2, "relative", 0.0002L, 0.0L, 0.1L, 0.0001L},
            {"revelation_weight", 3, "relative", 0.5L, 0.0L, 1.0L, 0.05L},
        }},
    };
    static constexpr std::size_t PARAM_COUNT = DESCRIPTOR.size();
    static constexpr const char* NAME = DESCRIPTOR.name.data();
    static constexpr bool USES_NATIVE_FEE = false;
    static constexpr unsigned REVELATION_ROUNDS = 1;

    struct State {
        std::array<T, 2> xp{T(0), T(0)};
        T price_scale{T(0)};
        T D{T(0)};
    };

    static T base_fee(const PolicyConfig<T>& params) { return params.params[0]; }
    static T capture(const PolicyConfig<T>& params) { return params.params[1]; }
    static T arb_cost(const PolicyConfig<T>& params) { return params.params[2]; }
    static T revelation_weight(const PolicyConfig<T>& params) { return params.params[3]; }

    static void validate_params(
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config
    ) {
        if (params.n_params != PARAM_COUNT) {
            throw std::invalid_argument("revealed-fair fee policy requires 4 parameters");
        }
        static_assert(
            std::is_floating_point_v<T>,
            "revealed-fair experiment currently requires floating arithmetic"
        );
        const T fee = base_fee(params);
        const T share = capture(params);
        const T cost = arb_cost(params);
        const T weight = revelation_weight(params);
        if (!(config.precision > T(0)) || !(config.fee_precision > T(0))) {
            throw std::invalid_argument("revealed-fair fee policy requires positive precisions");
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
        if (weight < T(0) || weight > config.precision) {
            throw std::invalid_argument("revelation weight");
        }
        if (!std::isfinite(fee) || !std::isfinite(share) ||
            !std::isfinite(cost) || !std::isfinite(weight)) {
            throw std::invalid_argument("revealed-fair fee parameters must be finite");
        }
    }

    static T gross_output_xp(
        const State& state,
        const PolicyPoolConfig<T>& config,
        std::size_t input_coin,
        const T& input_xp
    ) {
        const std::size_t output_coin = 1 - input_coin;
        auto trial = state.xp;
        trial[input_coin] += input_xp;
        const T y = MathOps<T>::get_y_unchecked(
            config.A, config.gamma, trial, state.D, output_coin
        );
        if (!(y < state.xp[output_coin])) return T(0);
        return state.xp[output_coin] - y;
    }

    static T fee_from_revealed_fair(
        const State& state,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config,
        std::size_t input_coin,
        const T& input_xp,
        const T& gross_output,
        unsigned round
    ) {
        if (round == 0) return base_fee(params);

        const T relative_step = input_xp * T(1e-4);
        const T absolute_step = state.xp[input_coin] * T(1e-10);
        const T step = std::max(relative_step, absolute_step);
        const T input_lo = std::max(input_xp - step, input_xp * T(0.5));
        const T input_hi = input_xp + step;
        if (!(input_hi > input_lo) || !(input_lo > T(0))) {
            return base_fee(params);
        }

        const auto net_output = [&](const T& q) {
            const T gross = gross_output_xp(state, config, input_coin, q);
            if (!(gross > T(0))) return T(0);
            const T fee = fee_from_revealed_fair(
                state, params, config, input_coin, q, gross, round - 1
            );
            return gross * (config.fee_precision - fee) / config.fee_precision;
        };
        const T net_lo = net_output(input_lo);
        const T net_hi = net_output(input_hi);
        if (!(net_hi > net_lo)) return config.fee_precision;
        const T marginal_net = (net_hi - net_lo) / (input_hi - input_lo);
        if (!(marginal_net > T(0))) return config.fee_precision;

        const T cost = arb_cost(params) / config.precision;
        T fair_normalized = T(0);
        if (input_coin == 0) {
            fair_normalized = T(1) / ((T(1) - cost) * marginal_net);
        } else {
            fair_normalized = marginal_net / (T(1) + cost);
        }
        if (!(fair_normalized > T(0)) || !std::isfinite(fair_normalized)) {
            return config.fee_precision;
        }

        auto trial = state.xp;
        trial[input_coin] += input_xp;
        trial[1 - input_coin] = state.xp[1 - input_coin] - gross_output;
        const T p_old_normalized = MathOps<T>::get_p(
            state.xp, state.D, {config.A, config.gamma}
        ) / config.precision;
        const T p_trial_normalized = MathOps<T>::get_p(
            trial, state.D, {config.A, config.gamma}
        ) / config.precision;
        const bool admitted = input_coin == 0
            ? p_old_normalized < fair_normalized &&
                p_trial_normalized > p_old_normalized &&
                p_trial_normalized <= fair_normalized
            : p_old_normalized > fair_normalized &&
                p_trial_normalized < p_old_normalized &&
                p_trial_normalized >= fair_normalized;
        if (!admitted) return config.fee_precision;

        T value_in = T(0);
        T value_out = T(0);
        if (input_coin == 0) {
            value_in = input_xp;
            value_out = gross_output * fair_normalized;
        } else {
            value_in = input_xp * fair_normalized;
            value_out = gross_output;
        }

        T fee = base_fee(params);
        if (value_out > value_in) {
            const T edge =
                (value_out - value_in) * config.fee_precision / value_out;
            if (edge > fee) {
                fee += (edge - fee) * capture(params) / config.precision;
            }
        }
        const T target = std::min(fee, config.fee_precision);
        const T previous = fee_from_revealed_fair(
            state, params, config, input_coin, input_xp, gross_output, round - 1
        );
        return previous + (target - previous) *
            revelation_weight(params) / config.precision;
    }

    static T get_fee(
        const State& state,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config,
        const PolicyResearchContext<T>&,
        const std::array<T, 2>& xp_new
    ) {
        const auto& xp_old = state.xp;
        std::size_t input_coin = 0;
        if (xp_new[0] > xp_old[0] && xp_new[1] < xp_old[1]) {
            input_coin = 0;
        } else if (xp_new[1] > xp_old[1] && xp_new[0] < xp_old[0]) {
            input_coin = 1;
        } else {
            return T(0);
        }
        if (!(state.D > T(0)) || !(state.price_scale > T(0)) ||
            !(xp_old[0] > T(0)) || !(xp_old[1] > T(0))) {
            return config.fee_precision;
        }

        const T input_xp = xp_new[input_coin] - xp_old[input_coin];
        const T gross_output = xp_old[1 - input_coin] - xp_new[1 - input_coin];
        if (!(input_xp > T(0)) || !(gross_output > T(0))) {
            return config.fee_precision;
        }
        return fee_from_revealed_fair(
            state, params, config, input_coin, input_xp, gross_output,
            REVELATION_ROUNDS
        );
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
