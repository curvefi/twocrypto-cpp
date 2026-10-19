// report_lean: the frozen research policy in one file, written to be ported line by line to Vyper
// (twocrypto-ng/contracts/main/YBOraclizedPolicy.vy). Floating-point research model; every equation below names its
// uint256 form. Vyper conventions: PRECISION = 10**18 (prices, EMAs, caps, weights), FEE_PRECISION = 10**10 (fees),
// timestamps in seconds (reports in microseconds on chain: observation_us / 10**6 here).
//
// Price scale (trend-gated dual EMA, "arch A"):
//   obs     = the admissible report delivered with this touch, else the pool's post-trade last_prices
//   fast    = EMA(obs, fast_half_life_s), slow = EMA(obs, slow_half_life_s), obs clamped to [scale / 2, 2 scale]
//   w       = min(1, |fast / slow - 1| / trend_gate)                      (0 when trend_gate = 0)
//   target  = fast + w (latest - fast),   latest = this touch's report, else the stored one
//   move    = min(|target - scale| / 2, step_cap * scale)
//   return scale +- TWEAK_PRICE_MULTIPLIER * move                        (the pool's tweak_price moves 1/5 of the way)
//
// Fee (report capture over base_fee, sqrt ageing to fallback_fee):
//   fresh   = min(F, base_fee + capture * max(0, edge - base_fee)), edge = whole-trade output-normalized edge vs report
//   w_age   = sqrt(clamp(age / relax_time_s, 0, 1)),         age = block.timestamp - observation (negative: 0)
//   fee     = (1 - w_age) fresh + w_age F   for a corrective swap (spot moves toward the report without passing it);
//             F (fallback_fee) for every other swap, with no usable report, or on a liquidity operation.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "pools/twocrypto_fx/policy_descriptor.hpp"
#include "pools/twocrypto_fx/policy_types.hpp"
#include "pools/twocrypto_fx/stableswap_math.hpp"

namespace arb::pools::twocrypto_fx {

template <typename T> struct ChallengeFeePolicy {
    static_assert(std::is_floating_point_v<T>, "report_lean is a floating-point research model");

    inline static constexpr PolicyDescriptor<8> DESCRIPTOR{"report_lean", {{
        {"base_fee", 0, "relative", 0.003L, 0.00001L, 0.1L, 0.00001L},
        {"capture", 1, "dimensionless", 0.80L, 0.0L, 1.0L, 0.005L},
        {"fallback_fee", 2, "relative", 0.015L, 0.001L, 0.1L, 0.0001L},
        {"relax_time_s", 3, "seconds", 300.0L, 1.0L, 3600.0L, 1.0L},
        {"fast_half_life_s", 4, "seconds", 3600.0L, 60.0L, 604800.0L, 1.0L},
        {"slow_half_life_s", 5, "seconds", 10800.0L, 600.0L, 2592000.0L, 1.0L},
        {"trend_gate", 6, "relative", 0.008L, 0.0L, 0.1L, 0.0001L},
        {"step_cap", 7, "relative", 0.0012L, 0.00001L, 0.03L, 0.00001L},
    }}};
    static constexpr std::size_t PARAM_COUNT = DESCRIPTOR.size();
    static constexpr const char *NAME = DESCRIPTOR.name.data();
    static constexpr bool USES_NATIVE_FEE = false, USES_SWAP_REPORTS = true, USES_CACHED_REPORTS = true;

    // Vyper: REPORT_PAST_WINDOW_US = 12_000_000 (assert observation_us + window >= slot_us).
    static constexpr double ADMISSION_S = 12.0;
    // Vyper: REPORT_FUTURE_WINDOW_US = 6_000_000 (message_us <= slot_us + window; observation_us <= message_us).
    static constexpr double FUTURE_WINDOW_S = 6.0;
    // Vyper: half-life EMAs as alpha = math._wad_exp(-dt * LN2_WAD // half_life), LN2_WAD = 693147180559945309.
    static constexpr long double LN2 = 0.693147180559945309L;
    // Vyper: the pool's tweak_price moves price_scale by (target - scale) / 5, so the policy returns scale +- 5 * move.
    static constexpr int TWEAK_PRICE_MULTIPLIER = 5;
    // Vyper: half-gap pursuit (gap // 2) before applying step_cap.
    static constexpr long double PURSUIT_GAIN = 0.5L;

    // Vyper: struct Report {asset_price: uint256, observation_us: uint256}.
    struct Report {
        T price{T(0)};
        double timestamp{0};  // observation time (seconds), never the submission time
    };
    struct State {
        // Committed pool state at the last update (Vyper: State.xp, State.D, State.price_scale), at price_scale.
        std::array<T, 2> xp{T(0), T(0)};
        T D{T(0)};
        T price_scale{T(0)};
        // Last admitted report (Vyper: self.report); a delivered report must be strictly newer.
        Report report{};
        // EMA memory (Vyper: State.last_update_ts, State.last_prices, State.fast_ema, State.slow_ema): `last_prices`
        // is the observation of the last update, which the EMAs integrate over [last_update_ts, now].
        uint64_t last_update_ts{0};
        T last_prices{T(0)};
        T fast_ema{T(0)};
        T slow_ema{T(0)};
        // Derived at every update from the fields above (Vyper: recomputed, or stored to save gas): the committed spot
        // _get_p(xp, D) * price_scale.
        T spot{T(0)};
        // Non-semantic memoization (no on-chain counterpart): EMA alphas of the last dt, shared by the target call and
        // the update of the same touch. Keep it last: tests compare the semantic bytes before it.
        struct AlphaCache {
            uint64_t dt{0};
            T fast_hl{T(0)}, slow_hl{T(0)}, fast{T(0)}, slow{T(0)};
        };
        mutable AlphaCache alpha_cache{};
    };

    // ---------------------------------------------------------------- parameters
    static void validate_params(const PolicyConfig<T> &p, const PolicyPoolConfig<T> &c) {
        if (p.n_params != PARAM_COUNT) throw std::invalid_argument("report_lean parameter count");
        for (std::size_t i = 0; i < PARAM_COUNT; ++i) {
            const auto &d = DESCRIPTOR.parameters[i];
            const T v = p.params[i];
            if (!std::isfinite(v) || v < T(d.minimum) || v > T(d.maximum))
                throw std::invalid_argument("report_lean parameter out of bounds");
        }
        if (c.precision != T(1) || c.fee_precision != T(1)) throw std::invalid_argument("report_lean needs unit precision");
        if (p.params[0] > p.params[2]) throw std::invalid_argument("report_lean base_fee above fallback_fee");
        if (p.params[4] > p.params[5]) throw std::invalid_argument("report_lean half-life order");
    }

    // ---------------------------------------------------------------- reports
    // A delivered report is admissible iff it is finite and positive, at most ADMISSION_S old and at most FUTURE_WINDOW_S
    // ahead of the block (age = block - observation in [-6 s, 12 s]), within the harness' report_max_age_s, and strictly
    // newer than the stored one. Vyper: the asserts of _verify_report; an inadmissible report reverts there, which the
    // simulator models as a touch without a report.
    static bool admissible(const Report &stored, const PolicyResearchContext<T> &x) {
        const double age = double(x.block_timestamp) - x.price_feed_timestamp;
        return x.price_feed > T(0) && std::isfinite(x.price_feed) && age >= -FUTURE_WINDOW_S && age <= ADMISSION_S &&
               age <= x.report_max_age_s && (!(stored.price > T(0)) || x.price_feed_timestamp > stored.timestamp);
    }
    // This touch's admitted report price, 0 when none.
    static T delivered(const State &s, const PolicyResearchContext<T> &x) { return admissible(s.report, x) ? x.price_feed : T(0); }

    // ---------------------------------------------------------------- fee
    // Effective report terms for input coin `coin_in` (the same for both coins: no toward/away split). The effective
    // report is the admissible delivered one, else the stored one; usable while -6 s <= age < relax_time_s, with ageing
    // weight w = sqrt(clamp(age / relax_time_s, 0, 1)). Vyper (_reported_fee): fresh + isqrt(delta^2 * age_us / expiry_us),
    // delta = F - fresh, which is (1 - w) fresh + w F; age 0 for observations at or after block.timestamp.
    static PolicyReportTerms<T> report_terms(const State &s, const PolicyConfig<T> &p, const PolicyPoolConfig<T> &,
                                             const PolicyResearchContext<T> &x, std::size_t) {
        const Report r = admissible(s.report, x) ? Report{x.price_feed, x.price_feed_timestamp} : s.report;
        const double age = double(x.block_timestamp) - r.timestamp, relax = double(p.params[3]);
        const bool usable = r.price > T(0) && age >= -FUTURE_WINDOW_S && age < relax;
        const T weight = usable ? T(std::sqrt(std::clamp(age / relax, 0.0, 1.0))) : T(1);
        const T F = p.params[2];
        return {usable, r.price, weight, p.params[0], p.params[1], F, T(0), T(0), T(0), F};
    }
    // Fee of a fresh report at whole-trade edge e: the capture hinge above the base, capped at F.
    static T fresh_fee(const PolicyReportTerms<T> &t, const T &edge) {
        return std::min(t.cap, t.base + t.capture * std::max(T(0), edge - t.base));
    }
    // Whole-trade edge of the swap old -> xp against the report, normalized by the output value (both legs valued at
    // the report: e = 1 - input / output). Vyper: the same on the swap's dx / dy at price_scale.
    static T reported_edge(const State &s, const T &price, const std::array<T, 2> &xp, std::size_t coin_in) {
        const std::size_t coin_out = 1 - coin_in;
        T input_value = xp[coin_in] - s.xp[coin_in];
        T output_value = s.xp[coin_out] - xp[coin_out];
        if (coin_in == 1) input_value = input_value * price / s.price_scale;
        else output_value = output_value * price / s.price_scale;
        if (!(output_value > input_value)) return T(0);
        return T(1) - input_value / output_value;
    }
    // The swap moves the spot toward the report without passing it (Vyper: _get_p before and after, times price_scale).
    // `before` is the committed spot, cached at the last update.
    static bool corrective(const State &s, const PolicyPoolConfig<T> &c, const T &price, const std::array<T, 2> &xp,
                           std::size_t coin_in) {
        if (!(xp[0] > T(0) && xp[1] > T(0))) return false;
        const std::array<T, 2> A_gamma{c.A, T(0)};
        const T before = s.spot > T(0) ? s.spot : MathOps<T>::get_p(s.xp, s.D, A_gamma) * s.price_scale;
        const T after = MathOps<T>::get_p(xp, s.D, A_gamma) * s.price_scale;
        if (!(before > T(0) && after > T(0))) return false;
        return coin_in == 0 ? before < after && after <= price : before > after && after >= price;
    }

    // Ephemeral fee quote, valid while state, parameters and context are frozen.
    struct Quote {
        const State *state;
        const PolicyPoolConfig<T> *config;
        PolicyReportTerms<T> terms;
        T live_D;
        T operator()(const std::array<T, 2> &xp) const {
            const auto &s = *state;
            const auto &old = s.xp;
            const T F = terms.fallback;
            // Guards pay F: no usable report, not one coin in and the other out, no committed state, live D moved.
            if (!terms.usable || (xp[0] >= old[0] && xp[1] >= old[1]) || (xp[0] <= old[0] && xp[1] <= old[1]) ||
                !(s.D > T(0)) || !(s.price_scale > T(0)) || !(old[0] > T(0) && old[1] > T(0)) || live_D != s.D)
                return F;
            const std::size_t coin_in = xp[0] > old[0] ? 0 : 1;
            const T edge = reported_edge(s, terms.price, xp, coin_in);
            if (!(edge > T(0))) return F;
            const T fresh = fresh_fee(terms, edge);
            return corrective(s, *config, terms.price, xp, coin_in) ? (T(1) - terms.weight) * fresh + terms.weight * F : F;
        }
    };
    static Quote prepare_fee(const State &s, const PolicyConfig<T> &p, const PolicyPoolConfig<T> &c,
                             const PolicyResearchContext<T> &x, const T &live_D) {
        return {&s, &c, report_terms(s, p, c, x, 0), live_D};
    }
    static T get_fee(const State &s, const PolicyConfig<T> &p, const PolicyPoolConfig<T> &c,
                     const PolicyResearchContext<T> &x, const std::array<T, 2> &xp, const T &live_D) {
        return prepare_fee(s, p, c, x, live_D)(xp);
    }
    // Sizing floors for the arbitrageur (no on-chain counterpart): every fee is >= base_fee; in a context every fee is
    // >= the aged base (1 - w) b + w F, and F when no swap of coin_in can be corrective (the effective report is not
    // beyond the committed spot in the swap's direction).
    static T fee_floor(const PolicyConfig<T> &p, const PolicyPoolConfig<T> &, const T &) { return p.params[0]; }
    static T context_fee_floor(const State &s, const PolicyConfig<T> &p, const PolicyPoolConfig<T> &c,
                               const PolicyResearchContext<T> &x, std::size_t coin_in) {
        const auto t = report_terms(s, p, c, x, coin_in);
        if (!t.usable) return t.fallback;
        if (s.spot > T(0) && (coin_in == 0 ? !(t.price > s.spot) : !(t.price < s.spot))) return t.fallback;
        return (T(1) - t.weight) * t.base + t.weight * t.fallback;
    }
    // Whether any size can profit (see report_fee.hpp): with edge e <= spot edge, h(e) = 1 - rho (1 - e) - fee(e) is
    // decided by its value at the spot edge.
    static bool context_may_profit(const State &s, const PolicyConfig<T> &p, const PolicyPoolConfig<T> &c,
                                   const PolicyResearchContext<T> &x, std::size_t i, T spot, T external) {
        if (!(spot > T(0)) || !(external > T(0)) || !std::isfinite(spot) || !std::isfinite(external)) return true;
        const auto t = report_terms(s, p, c, x, i);
        const T F = t.fallback, slack = T(1e-9);
        const T edge = !t.usable ? T(0) : i == 0 ? T(1) - spot / t.price : T(1) - t.price / spot;
        if (!(edge > T(0)))
            return i == 0 ? (T(1) - F) * external > spot * (T(1) - slack) : (T(1) - F) * spot > external * (T(1) - slack);
        const T rho = i == 0 ? t.price / external : external / t.price;
        const T fresh = fresh_fee(t, edge);
        return T(1) - rho * (T(1) - edge) - ((T(1) - t.weight) * fresh + t.weight * std::max(F, fresh)) > -slack;
    }

    // ---------------------------------------------------------------- price scale
    // EMA of `obs` held over dt = now - last_update_ts. Vyper: alpha = _wad_exp(-dt * LN2_WAD // half_life);
    // ema = (clamp(obs) * (PRECISION - alpha) + ema * alpha) // PRECISION; dt = 0 leaves the EMA unchanged.
    static T ema(const T &old, const T &obs, const T &alpha) { return obs * (T(1) - alpha) + old * alpha; }
    // The observation a touch integrates, clamped to [scale / 2, 2 scale] like YBTwocryptoPolicy's EMAs.
    static T clamp_obs(const T &obs, const T &scale) { return std::clamp(obs, scale / T(2), scale * T(2)); }
    // Both EMAs as of `now` with observation `obs`; the target call and the update of a touch share dt, so the alphas
    // are computed once.
    static std::array<T, 2> emas(const State &s, const T &obs, uint64_t now, const PolicyConfig<T> &p) {
        if (now <= s.last_update_ts) return {s.fast_ema, s.slow_ema};
        const uint64_t dt = now - s.last_update_ts;
        auto &a = s.alpha_cache;
        if (a.dt != dt || a.fast_hl != p.params[4] || a.slow_hl != p.params[5]) {
            a = {dt, p.params[4], p.params[5], std::exp(-T(LN2) * T(dt) / p.params[4]), std::exp(-T(LN2) * T(dt) / p.params[5])};
        }
        const T o = clamp_obs(obs, s.price_scale);
        return {ema(s.fast_ema, o, a.fast), ema(s.slow_ema, o, a.slow)};
    }

    // A pure read of the committed state (on chain a staticcall from tweak_price): update_state integrates the same
    // observation whether or not the pool's gates let this call happen.
    static T get_price_scale(const State &s, PolicyResearchContext<T> &x, const PolicyConfig<T> &p, const PolicyPoolConfig<T> &) {
        const T scale = s.price_scale;
        if (scale == T(0)) return T(0);
        const T report = delivered(s, x);
        // This touch's observation: the report, else the pool's post-trade last_prices (Vyper: staticcall pool.last_prices()).
        const T obs = report > T(0) ? report : x.pool_last_prices > T(0) ? x.pool_last_prices : s.last_prices;
        const auto [fast, slow] = emas(s, obs, x.block_timestamp, p);
        const T latest = obs;
        // Trend gate: the fast EMA in a range, this touch's observation (report or last_prices) once EMAs are trend_gate apart.
        T target = fast;
        if (p.params[6] > T(0) && latest > T(0) && slow > T(0)) {
            const T w = std::min(T(1), std::abs(fast / slow - T(1)) / p.params[6]);
            target = fast + w * (latest - fast);
        }
        // Step cap per touch (half-gap pursuit), scaled up by the pool's 1/5 (Vyper: min(gap // 2, scale * step_cap // PRECISION) * 5).
        const T move = std::min(T(PURSUIT_GAIN) * std::abs(target - scale), scale * p.params[7]);
        return target >= scale ? scale + T(TWEAK_PRICE_MULTIPLIER) * move : scale - T(TWEAK_PRICE_MULTIPLIER) * move;
    }

    static void update_state(State &s, PolicyResearchContext<T> &x, const PolicyConfig<T> &p, const PolicyPoolConfig<T> &c,
                             const PolicyUpdate<T> &u) {
        // An inadmissible report is a touch without one (on chain the swap reverts and the arb resubmits without it).
        const T report = delivered(s, x);
        const uint64_t now = x.block_timestamp;
        if (report > T(0)) s.report = {report, x.price_feed_timestamp};
        // This touch's observation: the report, else the pool's post-trade last_prices.
        const T observed = report > T(0) ? report : u.last_prices;
        if (s.price_scale == T(0)) {
            s.fast_ema = s.slow_ema = u.price_oracle;  // first touch: both EMAs start at the pool's oracle
        } else {
            const auto e = emas(s, observed, now, p);   // clamped at the scale the EMAs ran at
            s.fast_ema = e[0];
            s.slow_ema = e[1];
        }
        s.last_update_ts = now;
        s.last_prices = observed;
        s.price_scale = u.price_scale;
        s.xp = u.xp;
        s.D = u.D;
        s.spot = s.D > T(0) && s.xp[0] > T(0) && s.xp[1] > T(0) ? MathOps<T>::get_p(s.xp, s.D, {c.A, T(0)}) * s.price_scale : T(0);
    }
};

}  // namespace arb::pools::twocrypto_fx
