// Fee path of twocrypto-ng/contracts/main/YBOraclizedPolicy.vy.
// Report delivery remains an atomic research action supplied by the harness.
#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

#include "pools/twocrypto_fx/policies/sqrt_fair_fee.hpp"
#include "pools/twocrypto_fx/stableswap_math.hpp"

namespace arb::pools::twocrypto_fx {

template <typename T>
struct OraclizedFairFeePolicy {
    using Base = SqrtFairFeePolicy<T, 11>;
    using Report = typename Base::Report;

    struct State {
        typename Base::State base{};
        T D{0};
    };

    static void validate_params(const PolicyConfig<T>& params,
                                const PolicyPoolConfig<T>& config) {
        Base::validate_params(params, config);
        if (params.params[0] < config.fee_precision / T(100000) ||
            params.params[0] > params.params[2])
            throw std::invalid_argument("oraclized base fee must be positive and at most fallback");
        if (!(params.params[4] > T(0) && params.params[4] <= T(12)))
            throw std::invalid_argument("oraclized report admission age must be in (0, 12] seconds");
    }

    static T fallback(const PolicyConfig<T>& params, const PolicyPoolConfig<T>& config) {
        return Base::fallback(params, config);
    }

    static PolicyResearchContext<T> bounded_context(const PolicyConfig<T>& params,
                                                    const PolicyResearchContext<T>& context) {
        auto bounded = context;
        bounded.report_max_age_s = std::min(
            bounded.report_max_age_s, static_cast<double>(params.params[4]));
        const T min_price = [] {
            if constexpr (std::is_same_v<T, uint256>) return T(1000000);
            else return T(1e-12);
        }();
        const T max_price = [] {
            if constexpr (std::is_same_v<T, uint256>) return T("1000000000000000000000000000000");
            else return T(1e12);
        }();
        if (bounded.price_feed > T(0) &&
            (bounded.price_feed < min_price || bounded.price_feed > max_price))
            bounded.price_feed = T(0);
        return bounded;
    }

    static Report effective_report(const State& state, const PolicyConfig<T>& params,
                                   const PolicyResearchContext<T>& context) {
        return Base::effective_report(state.base, bounded_context(params, context));
    }

    static T reported_edge(const State& state, const Report& report,
                           const PolicyPoolConfig<T>& config,
                           const std::array<T, 2>& xp, std::size_t coin_in) {
        const std::size_t coin_out = 1 - coin_in;
        T input_value = xp[coin_in] - state.base.xp[coin_in];
        T output_value = state.base.xp[coin_out] - xp[coin_out];
        if (coin_in == 1)
            input_value = input_value * report.price / state.base.price_scale;
        else
            output_value = output_value * report.price / state.base.price_scale;
        if (!(output_value > input_value)) return T(0);
        // Vyper computes FEE_PRECISION - floor(input * FEE_PRECISION / output).
        return config.fee_precision - input_value * config.fee_precision / output_value;
    }

    static bool corrective(const State& state, const PolicyPoolConfig<T>& config,
                           const Report& report, const std::array<T, 2>& xp,
                           std::size_t coin_in) {
        if (!(xp[0] > T(0) && xp[1] > T(0))) return false;
        const std::array<T, 2> A_gamma{config.A, T(0)};
        T before = MathOps<T>::get_p(state.base.xp, state.D, A_gamma);
        T after = MathOps<T>::get_p(xp, state.D, A_gamma);
        if (!(before > T(0) && after > T(0))) return false;
        if constexpr (std::is_same_v<T, uint256>) {
            const T largest = std::numeric_limits<T>::max() / state.base.price_scale;
            if (before > largest || after > largest) return false;
        }
        before = before * state.base.price_scale / config.precision;
        after = after * state.base.price_scale / config.precision;
        if (!(before > T(0) && after > T(0))) return false;
        return coin_in == 0 ? before < after && after <= report.price
                            : before > after && after >= report.price;
    }

    static T quoted_fee(const State& state, const PolicyConfig<T>& params,
                        const PolicyPoolConfig<T>& config, const Report& report,
                        const std::array<T, 2>& xp, const T& live_D,
                        double age, std::size_t& coin_in) {
        const T full = fallback(params, config);
        const auto& old = state.base.xp;
        if ((xp[0] >= old[0] && xp[1] >= old[1]) ||
            (xp[0] <= old[0] && xp[1] <= old[1])) return full;
        if (!(report.price > T(0)) || !std::isfinite(age) || age < 0 ||
            age >= static_cast<double>(params.params[3]) ||
            !(state.D > T(0)) || !(state.base.price_scale > T(0)) ||
            !(old[0] > T(0) && old[1] > T(0)) || live_D != state.D)
            return full;
        coin_in = xp[0] > old[0] ? 0 : 1;
        const T edge = reported_edge(state, report, config, xp, coin_in);
        if (!(edge > T(0))) return full;
        const T base = params.params[0];
        const T excess = edge > base ? edge - base : T(0);
        return std::min(full, T(base + params.params[1] * excess / config.precision));
    }

    static T get_fee(const State& state, const PolicyConfig<T>& params,
                     const PolicyPoolConfig<T>& config, const PolicyResearchContext<T>& context,
                     const std::array<T, 2>& xp, const T& live_D) {
        const Report report = effective_report(state, params, context);
        const double age = static_cast<double>(context.block_timestamp) - report.timestamp;
        std::size_t coin_in = 0;
        const T fresh = quoted_fee(state, params, config, report, xp, live_D, age, coin_in);
        const T fee = Base::inflate(fresh, report, params, config, context);
        if (fee >= fallback(params, config) || !corrective(state, config, report, xp, coin_in))
            return fallback(params, config);
        return std::max(fee, params.params[0]);
    }

    static auto prepare_fee(const State& state, const PolicyConfig<T>& params,
                            const PolicyPoolConfig<T>& config,
                            const PolicyResearchContext<T>& context, const T& live_D) {
        if constexpr (std::is_floating_point_v<T>) {
            const Report report = effective_report(state, params, context);
            const double age = static_cast<double>(context.block_timestamp) - report.timestamp;
            const T full = fallback(params, config);
            const bool expired = !(report.price > T(0)) || !std::isfinite(age) ||
                                 age < 0 || age >= static_cast<double>(params.params[3]);
            const T weight = expired ? T(1) : T(std::sqrt(age / static_cast<double>(params.params[3])));
            return [&, params, report, full, weight, expired, age, live_D](const std::array<T, 2>& xp) -> T {
                if (expired) return full;
                std::size_t coin_in = 0;
                const T fresh = quoted_fee(state, params, config, report, xp, live_D, age, coin_in);
                const T fee = fresh + (full - fresh) * weight;
                return fee >= full || !corrective(state, config, report, xp, coin_in)
                    ? full : std::max(fee, params.params[0]);
            };
        } else {
            return [&, params, live_D](const std::array<T, 2>& xp) {
                return get_fee(state, params, config, context, xp, live_D);
            };
        }
    }

    // Base fee is a lower bound. On the floating sizing path the corrective
    // test can only increase the old edge-only fee, so its no-profit proof is
    // still conservative. The integer path never rejects in that proof.
    static T fee_floor(const PolicyConfig<T>& params, const PolicyPoolConfig<T>& config, const T& native) {
        return Base::fee_floor(params, config, native);
    }
    static T context_fee_floor(const State& state, const PolicyConfig<T>& params,
                               const PolicyPoolConfig<T>& config,
                               const PolicyResearchContext<T>& context, std::size_t input_coin) {
        return Base::context_fee_floor(state.base, params, config,
                                       bounded_context(params, context), input_coin);
    }
    static bool context_may_profit(const State& state, const PolicyConfig<T>& params,
                                   const PolicyPoolConfig<T>& config,
                                   const PolicyResearchContext<T>& context,
                                   std::size_t input_coin, T spot, T external) {
        return Base::context_may_profit(state.base, params, config,
                                        bounded_context(params, context),
                                        input_coin, spot, external);
    }
    static void update_state(State& state, PolicyResearchContext<T>& context,
                             const PolicyConfig<T>& params,
                             [[maybe_unused]] const PolicyPoolConfig<T>& config,
                             const PolicyUpdate<T>& update) {
        const Report offered = effective_report(state, params, context);
        if (offered.price > T(0) &&
            (!(state.base.report.price > T(0)) || offered.timestamp > state.base.report.timestamp))
            state.base.report = offered;
        state.base.xp = update.xp;
        state.base.price_scale = update.price_scale;
        state.D = update.D;
    }
};

} // namespace arb::pools::twocrypto_fx
