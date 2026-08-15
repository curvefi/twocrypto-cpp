// Exact C++ translation of reference/twocrypto-ng/contracts/main/YBTwocryptoPolicy.vy.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "pools/twocrypto_fx/policies/common.hpp"
#include "pools/twocrypto_fx/stableswap_math.hpp"

namespace arb::pools::twocrypto_fx {

template <typename T>
struct ChallengeFeePolicy {
    static constexpr std::size_t PARAM_COUNT = 6;
    static constexpr const char* NAME = "yieldbasis_twocrypto_policy";
    static constexpr uint64_t CAP_RAMP_SECONDS = 3600;

    struct State {
        uint64_t last_update_ts{0};
        T last_prices{0};
        T fast_ema{0};
        T slow_ema{0};
        T price_scale{0};
    };

    static T precision() {
        if constexpr (std::is_same_v<T, uint256>) {
            return T("1000000000000000000");
        }
        return T(1);
    }

    static T param(const PolicyConfig<T>& params, std::size_t index) {
        if (params.n_params != PARAM_COUNT) {
            throw std::invalid_argument("policy requires exactly 6 parameters");
        }
        return params.params[index];
    }

    static uint64_t integer_param(
        const PolicyConfig<T>& params,
        std::size_t index,
        const char* name
    ) {
        const T value = param(params, index);
        if constexpr (std::is_same_v<T, uint256>) {
            return value.template convert_to<uint64_t>();
        } else {
            if (!std::isfinite(value) || value < T(0) || std::floor(value) != value) {
                throw std::invalid_argument(std::string(name) + " must be an integer");
            }
            return static_cast<uint64_t>(value);
        }
    }

    static uint64_t fast_half_life(const PolicyConfig<T>& params) {
        return integer_param(params, 0, "fast half-life");
    }

    static uint64_t slow_half_life(const PolicyConfig<T>& params) {
        return integer_param(params, 1, "slow half-life");
    }

    static T kappa(const PolicyConfig<T>& params) { return param(params, 2); }
    static T deadband(const PolicyConfig<T>& params) { return param(params, 3); }
    static T min_cap(const PolicyConfig<T>& params) { return param(params, 4); }
    static T max_cap(const PolicyConfig<T>& params) { return param(params, 5); }

    static void validate_params(const PolicyConfig<T>& params) {
        const uint64_t fast = fast_half_life(params);
        const uint64_t slow = slow_half_life(params);
        const T one = precision();
        if (fast < 600 || fast > 604800) {
            throw std::invalid_argument("fast half-life");
        }
        if (slow < 600 || slow > 604800) {
            throw std::invalid_argument("slow half-life");
        }
        if (fast > slow) {
            throw std::invalid_argument("half-life order");
        }
        if (kappa(params) > T(2) * one) {
            throw std::invalid_argument("kappa");
        }
        if (deadband(params) > T(60) * one / T(10000)) {
            throw std::invalid_argument("deadband");
        }
        if (max_cap(params) > T(60) * one / T(10000)) {
            throw std::invalid_argument("max cap");
        }
        if (min_cap(params) < one / T(10000) ||
            min_cap(params) > max_cap(params)) {
            throw std::invalid_argument("min cap");
        }
    }

    static T ema_alpha(uint64_t dt, uint64_t half_life) {
        if constexpr (std::is_same_v<T, uint256>) {
            const uint256 exponent_magnitude =
                uint256(dt) * uint256("693147180559945309") / uint256(half_life);
            return MathOps<uint256>::wad_exp(-int256(exponent_magnitude));
        } else {
            constexpr long double LN2 = 0.693147180559945309L;
            return T(std::exp(-LN2 * static_cast<long double>(dt) /
                              static_cast<long double>(half_life)));
        }
    }

    static T clamp_observation(T observation, T price_scale) {
        const T lower = price_scale / T(2);
        const T upper = price_scale * T(2);
        if (observation < lower) return lower;
        if (observation > upper) return upper;
        return observation;
    }

    static T ema(
        T old_value,
        T observation,
        T price_scale,
        uint64_t dt,
        uint64_t half_life
    ) {
        if (dt == 0) return old_value;
        const T price = clamp_observation(observation, price_scale);
        const T alpha = ema_alpha(dt, half_life);
        if constexpr (std::is_same_v<T, uint256>) {
            const T one = precision();
            return (price * (one - alpha) + old_value * alpha) / one;
        } else {
            return price * (T(1) - alpha) + old_value * alpha;
        }
    }

    static std::array<T, 2> get_emas(
        const State& state,
        uint64_t now,
        const PolicyConfig<T>& params
    ) {
        if (state.price_scale == T(0)) return {T(0), T(0)};
        if (now < state.last_update_ts) {
            throw std::underflow_error("policy timestamp");
        }
        const uint64_t dt = now - state.last_update_ts;
        return {
            ema(
                state.fast_ema,
                state.last_prices,
                state.price_scale,
                dt,
                fast_half_life(params)
            ),
            ema(
                state.slow_ema,
                state.last_prices,
                state.price_scale,
                dt,
                slow_half_life(params)
            ),
        };
    }

    static T raw_target(
        const State& state,
        uint64_t now,
        const PolicyConfig<T>& params
    ) {
        const auto emas = get_emas(state, now, params);
        const T fast = emas[0];
        const T slow = emas[1];
        const T one = precision();
        if (fast >= slow) {
            return slow + kappa(params) * (fast - slow) / one;
        }
        const T step = kappa(params) * (slow - fast) / one;
        return slow - (step < slow ? step : slow);
    }

    static T current_cap(
        const State& state,
        uint64_t now,
        const PolicyConfig<T>& params
    ) {
        if (now < state.last_update_ts) {
            throw std::underflow_error("policy timestamp");
        }
        const uint64_t elapsed = now - state.last_update_ts;
        const uint64_t ramp_time = elapsed < CAP_RAMP_SECONDS
            ? elapsed
            : CAP_RAMP_SECONDS;
        return min_cap(params) +
            (max_cap(params) - min_cap(params)) * T(ramp_time) / T(CAP_RAMP_SECONDS);
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
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>&
    ) {
        validate_params(params);
        const T current = state.price_scale;
        if (current == T(0)) return T(0);

        const T target = raw_target(state, research.block_timestamp, params);
        const T gap = target >= current ? target - current : current - target;
        const T one = precision();
        if (gap * one <= current * deadband(params)) return current;

        const T max_move = current * current_cap(
            state, research.block_timestamp, params
        ) / one;
        const T desired = gap < max_move ? gap : max_move;
        const T target_gap = T(5) * desired;
        return target >= current ? current + target_gap : current - target_gap;
    }

    static void update_state(
        State& state,
        PolicyResearchContext<T>& research,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>&,
        const PolicyUpdate<T>& update
    ) {
        validate_params(params);
        const uint64_t now = research.block_timestamp;
        if (now < state.last_update_ts) {
            throw std::underflow_error("policy timestamp");
        }

        T fast = update.price_oracle;
        T slow = update.price_oracle;
        if (state.price_scale != T(0)) {
            const auto emas = get_emas(state, now, params);
            fast = emas[0];
            slow = emas[1];
        }
        state.last_update_ts = now;
        state.last_prices = update.last_prices;
        state.fast_ema = fast;
        state.slow_ema = slow;
        state.price_scale = update.price_scale;
    }
};

} // namespace arb::pools::twocrypto_fx
