// Standalone square-root aging of the report discount, with public report memory.
#pragma once

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "pools/twocrypto_fx/policy_descriptor.hpp"
#include "pools/twocrypto_fx/policy_types.hpp"

namespace arb::pools::twocrypto_fx {

template <typename T, std::size_t ParameterCount = 4>
struct SqrtFairFeePolicy {
    inline static constexpr PolicyDescriptor<4> DESCRIPTOR{
        "sqrt_fair_fee", {{
            {"base_fee", 0, "relative", 0.005L, 0.0L, 1.0L, 0.0001L},
            {"capture", 1, "relative", 0.9L, 0.0L, 1.0L, 0.01L},
            {"fallback_fee", 2, "relative", 0.03L, 0.0L, 1.0L, 0.0001L},
            {"fee_inflation_seconds", 3, "seconds", 120.0L, 1.0L, 604800.0L, 1.0L},
        }},
    };

    struct Report {
        T price{T(0)};
        double timestamp{0}; // Original observation time, never the submission time.
    };

    struct State {
        std::array<T, 2> xp{T(0), T(0)};
        T price_scale{T(0)};
        Report report{};
    };

    static void validate_params(const PolicyConfig<T>& params,
                                const PolicyPoolConfig<T>& config) {
        if (params.n_params != ParameterCount)
            throw std::invalid_argument("sqrt fair fee parameter count");
        for (std::size_t i = 0; i < 3; ++i) {
            const T limit = i == 1 ? config.precision : config.fee_precision;
            const T value = params.params[i];
            if (!(value >= T(0) && value <= limit))
                throw std::invalid_argument("sqrt fair fee parameter out of range");
        }
        const T horizon = params.params[3];
        if (!(horizon >= T(1) && horizon <= T(604800)))
            throw std::invalid_argument("fee inflation horizon must be in [1, 604800] seconds");
        if constexpr (std::is_floating_point_v<T>) {
            if (std::floor(horizon) != horizon)
                throw std::invalid_argument("fee inflation horizon must be whole seconds");
        }
    }

    static bool admissible_new_report(const State& state,
                                      const PolicyResearchContext<T>& context) {
        const double age = static_cast<double>(context.block_timestamp) - context.price_feed_timestamp;
        if (!(context.price_feed > T(0)) || !std::isfinite(age) ||
            !std::isfinite(context.report_max_age_s) || age < 0 ||
            age > context.report_max_age_s || context.price_feed_timestamp < 0)
            return false;
        if constexpr (std::is_floating_point_v<T>) {
            if (!std::isfinite(context.price_feed)) return false;
        }
        return !(state.report.price > T(0)) || context.price_feed_timestamp > state.report.timestamp;
    }

    static Report effective_report(const State& state, const PolicyResearchContext<T>& context) {
        return admissible_new_report(state, context)
            ? Report{context.price_feed, context.price_feed_timestamp} : state.report;
    }

    static T fallback(const PolicyConfig<T>& params, const PolicyPoolConfig<T>& config) {
        // Zero selects native fees; preserve the pool's 0.1 bp noise floor.
        return std::max(config.fee_precision / T(100000), params.params[2]);
    }

    static T inflate(T quoted_fee, const Report& report, const PolicyConfig<T>& params,
                     const PolicyPoolConfig<T>& config, const PolicyResearchContext<T>& context) {
        const T full_fee = fallback(params, config);
        const double age = static_cast<double>(context.block_timestamp) - report.timestamp;
        const double horizon = static_cast<double>(params.params[3]);
        if (!(report.price > T(0)) || !std::isfinite(age) || age < 0 || age >= horizon)
            return full_fee;
        if constexpr (std::is_same_v<T, uint256>) {
            // Research timestamps have a microsecond lattice. Round the age and
            // fee increment upward, so rounding never grants extra discount.
            const auto elapsed_us = static_cast<uint64_t>(std::ceil(age * 1e6));
            const auto horizon_us = static_cast<uint64_t>(horizon * 1e6);
            const T discount = full_fee - quoted_fee;
            const T numerator = discount * discount * T(elapsed_us);
            // ceil(sqrt(discount^2 * age / horizon)), without a float sqrt.
            T increment = boost::multiprecision::sqrt(T(numerator / T(horizon_us)));
            if (increment * increment * T(horizon_us) < numerator) ++increment;
            return quoted_fee + increment;
        } else {
            return quoted_fee + (full_fee - quoted_fee) * T(std::sqrt(age / horizon));
        }
    }

    static T raw_fee(const State& state, const PolicyConfig<T>& params,
                     const PolicyPoolConfig<T>& config, const Report& report,
                     const std::array<T, 2>& xp_new) {
        const T full_fee = fallback(params, config);
        const bool sell_coin1 = xp_new[1] > state.xp[1] && xp_new[0] < state.xp[0];
        const bool sell_coin0 = xp_new[0] > state.xp[0] && xp_new[1] < state.xp[1];
        if ((!sell_coin0 && !sell_coin1) || !(state.price_scale > T(0)) ||
            !(report.price > T(0)))
            return full_fee;

        const T dx = sell_coin1 ? xp_new[1] - state.xp[1] : xp_new[0] - state.xp[0];
        T dy = sell_coin1 ? state.xp[0] - xp_new[0] : state.xp[1] - xp_new[1];
        if constexpr (std::is_same_v<T, uint256>) {
            if (dy <= T(1)) return full_fee;
            dy -= T(1);
        }
        // Both pre-fee legs valued in coin0; the fee is charged on output.
        const T coin1 = (sell_coin1 ? dx : dy) * config.precision / state.price_scale;
        const T value_in = sell_coin1 ? coin1 * report.price / config.precision : dx;
        const T value_out = sell_coin1 ? dy : coin1 * report.price / config.precision;
        const T edge = value_out > value_in
            ? (value_out - value_in) * config.fee_precision / value_out : T(0);
        const T base = params.params[0];
        const T excess = edge > base ? edge - base : T(0);
        const T quoted_fee = std::min(full_fee, std::max(config.fee_precision / T(100000),
            T(base + excess * params.params[1] / config.precision)));
        return quoted_fee;
    }

    static T get_fee(const State& state, const PolicyConfig<T>& params,
                     const PolicyPoolConfig<T>& config, const PolicyResearchContext<T>& context,
                     const std::array<T, 2>& xp_new) {
        const auto report = effective_report(state, context);
        return inflate(raw_fee(state, params, config, report, xp_new), report, params, config, context);
    }

    // Ephemeral quote: valid only while state, parameters and context are frozen.
    // Keep the integer rounding path unchanged; only floating sizing prepares age.
    static auto prepare_fee(const State& state, const PolicyConfig<T>& params,
                            const PolicyPoolConfig<T>& config, const PolicyResearchContext<T>& context) {
        if constexpr (std::is_floating_point_v<T>) {
            const auto report = effective_report(state, context);
            const T full = fallback(params, config);
            const double age = static_cast<double>(context.block_timestamp) - report.timestamp;
            const double horizon = static_cast<double>(params.params[3]);
            const bool expired = !(report.price > T(0)) || !std::isfinite(age) || age < 0 || age >= horizon;
            const T weight = expired ? T(1) : T(std::sqrt(age / horizon));
            return [&, report, full, weight, expired](const std::array<T, 2>& xp) -> T {
                if (expired) return full;
                const T q = raw_fee(state, params, config, report, xp);
                return q + (full - q) * weight;
            };
        } else {
            return [&](const std::array<T, 2>& xp) { return get_fee(state, params, config, context, xp); };
        }
    }

    static T fee_floor(const PolicyConfig<T>& params, const PolicyPoolConfig<T>& config, const T&) {
        return std::min(fallback(params, config),
                        std::max(config.fee_precision / T(100000), params.params[0]));
    }

    static T context_fee_floor(const State& state, const PolicyConfig<T>& params,
                               const PolicyPoolConfig<T>& config,
                               const PolicyResearchContext<T>& context, std::size_t) {
        return inflate(fee_floor(params, config, T(0)), effective_report(state, context),
                       params, config, context);
    }

    // False only when no swap from input_coin can profit at this state and
    // context. spot is the pool marginal price and external the best external
    // execution price net of external fees (bid for input 0, ask for input 1),
    // both coin0 per coin1. Execution only worsens with size, so a swap's report
    // edge e never exceeds its spot value, and profit requires
    // g(e) = 1 - rho * (1 - e) - fee(e) > 0. g is piecewise affine and rises
    // below its first kink, so the spot edge and the kinks bound every size.
    static bool context_may_profit(const State& state, const PolicyConfig<T>& params,
                                   const PolicyPoolConfig<T>& config,
                                   const PolicyResearchContext<T>& context,
                                   std::size_t input_coin, T spot, T external) {
        if constexpr (!std::is_floating_point_v<T>) {
            return true;
        } else {
            if (!(spot > T(0)) || !(external > T(0)) || !std::isfinite(spot) || !std::isfinite(external))
                return true;
            const T full = fallback(params, config);
            const Report report = effective_report(state, context);
            const double age = static_cast<double>(context.block_timestamp) - report.timestamp;
            const double horizon = static_cast<double>(params.params[3]);
            const bool expired = !(report.price > T(0)) || !std::isfinite(age) || age < 0 || age >= horizon;
            const T weight = expired ? T(1) : T(std::sqrt(age / horizon));
            const T slack = T(1e-9); // Marginal-price rounding; errs toward sizing.
            if (expired)
                return input_coin == 0 ? (T(1) - full) * external > spot * (T(1) - slack)
                                       : (T(1) - full) * spot > external * (T(1) - slack);
            const T rho = input_coin == 0 ? report.price / external : external / report.price;
            const T spot_edge = input_coin == 0 ? T(1) - spot / report.price : T(1) - report.price / spot;
            const T base = params.params[0];
            const T capture = params.params[1] / config.precision;
            const T minimum = config.fee_precision / T(100000);
            const auto margin = [&](T edge) {
                const T excess = edge > base ? edge - base : T(0);
                const T quoted = std::min(full, std::max(minimum, T(base + excess * capture)));
                return T(1) - rho * (T(1) - edge) - (quoted + (full - quoted) * weight);
            };
            T best = std::max(margin(spot_edge), margin(std::min(spot_edge, base)));
            if (capture > T(0)) {
                best = std::max(best, margin(std::min(spot_edge, base + (full - base) / capture)));
                if (base < minimum)
                    best = std::max(best, margin(std::min(spot_edge, base + (minimum - base) / capture)));
            }
            return best > -slack;
        }
    }

    static void update_state(State& state, PolicyResearchContext<T>& context,
                             const PolicyConfig<T>&, const PolicyPoolConfig<T>&,
                             const PolicyUpdate<T>& update) {
        // The harness exposes a submitted report only during the native swap.
        // Quotes never get here; reverted transactions restore this value state.
        if (admissible_new_report(state, context))
            state.report = {context.price_feed, context.price_feed_timestamp};
        state.xp = update.xp;
        state.price_scale = update.price_scale;
    }
};

} // namespace arb::pools::twocrypto_fx
