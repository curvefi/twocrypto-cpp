// Exact C++ translation of reference/twocrypto-ng/contracts/main/YBTwocryptoPolicy.vy.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "pools/twocrypto_fx/policy_descriptor.hpp"
#include "pools/twocrypto_fx/policy_types.hpp"
#include "pools/twocrypto_fx/stableswap_math.hpp"

namespace arb::pools::twocrypto_fx {

template <typename T>
struct ChallengeFeePolicy {
    inline static constexpr PolicyDescriptor<6> DESCRIPTOR{
        "yieldbasis_twocrypto_policy",
        {{
            {"fast_half_life_s", 0, "seconds", 7500.0L, 600.0L, 604800.0L, 60.0L},
            {"slow_half_life_s", 1, "seconds", 54000.0L, 600.0L, 604800.0L, 600.0L},
            {"kappa", 2, "dimensionless", 1.27L, 0.0L, 2.0L, 0.01L},
            {"deadband", 3, "relative", 0.00028L, 0.0L, 0.006L, 0.00001L},
            {"min_cap", 4, "relative", 0.0012L, 0.0001L, 0.006L, 0.0001L},
            {"max_cap", 5, "relative", 0.0048L, 0.0001L, 0.006L, 0.0001L},
        }},
    };
    static constexpr std::size_t PARAM_COUNT = DESCRIPTOR.size();
    static constexpr const char* NAME = DESCRIPTOR.name.data();
    static constexpr bool USES_NATIVE_FEE = true;
    static constexpr uint64_t CAP_RAMP_SECONDS = 3600;

    struct EmaAlphaCache {
        bool valid{false};
        uint64_t dt{0};
        uint64_t fast_half_life{0};
        uint64_t slow_half_life{0};
        T fast_alpha{0};
        T slow_alpha{0};
    };

    struct State {
        uint64_t last_update_ts{0};
        T last_prices{0};
        T fast_ema{0};
        T slow_ema{0};
        T price_scale{0};
        // Non-semantic memoization: policy state JSON and onchain parity omit it.
        mutable EmaAlphaCache alpha_cache{};
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

    static void validate_params(
        const PolicyConfig<T>& params, const PolicyPoolConfig<T>& = {}
    ) {
        const uint64_t fast = fast_half_life(params);
        const uint64_t slow = slow_half_life(params);
        const T one = precision();
        T relative_cap = T(60) * one / T(10000);
        if constexpr (!std::is_same_v<T, uint256>) {
            // Protocol v1 widens binary64 candidates, including the 60 bps boundary.
            relative_cap = T(0.006);
        }
        if (fast < 600 || fast > 604800) {
            throw std::invalid_argument("fast half-life");
        }
        if (slow < 600 || slow > 604800) {
            throw std::invalid_argument("slow half-life");
        }
        if (fast > slow) {
            throw std::invalid_argument("half-life order");
        }
        if (kappa(params) < T(0) || kappa(params) > T(2) * one) {
            throw std::invalid_argument("kappa");
        }
        if (deadband(params) < T(0) || deadband(params) > relative_cap) {
            throw std::invalid_argument("deadband");
        }
        if (max_cap(params) > relative_cap) {
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
            const T exponent = -T(0.693147180559945309L) * T(dt) / T(half_life);
            using std::exp;
            return exp(exponent);
        }
    }

    static T clamp_observation(T observation, T price_scale) {
        const T lower = price_scale / T(2);
        const T upper = price_scale * T(2);
        if (observation < lower) return lower;
        if (observation > upper) return upper;
        return observation;
    }

    static T ema_with_alpha(
        T old_value,
        T observation,
        T price_scale,
        T alpha
    ) {
        const T price = clamp_observation(observation, price_scale);
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
        if (dt == 0) return {state.fast_ema, state.slow_ema};

        const uint64_t fast = fast_half_life(params);
        const uint64_t slow = slow_half_life(params);
        auto& cache = state.alpha_cache;
        if (
            !cache.valid || cache.dt != dt ||
            cache.fast_half_life != fast || cache.slow_half_life != slow
        ) {
            cache.valid = true;
            cache.dt = dt;
            cache.fast_half_life = fast;
            cache.slow_half_life = slow;
            cache.fast_alpha = ema_alpha(dt, fast);
            cache.slow_alpha = ema_alpha(dt, slow);
        }
        return {
            ema_with_alpha(
                state.fast_ema,
                state.last_prices,
                state.price_scale,
                cache.fast_alpha
            ),
            ema_with_alpha(
                state.slow_ema,
                state.last_prices,
                state.price_scale,
                cache.slow_alpha
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
