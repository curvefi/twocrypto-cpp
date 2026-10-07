// Report fee over the YB dual-EMA price scale.
//   fresh = min(F, b + c * max(0, e - b)), e = whole-trade output-normalized edge against the effective report.
//   fee   = (1 - w) * fresh + w * F,  w = sqrt(clamp((B - u + delta) / (T + delta), 0, 1)),  T = aging_s.
//   c = away_capture, times toward_ratio when the swap moves spot toward the committed price_scale.
//   A delivered report (P, u) is admissible iff -future_window <= B - u <= min(12 s, report_max_age_s), P is
//   finite and positive, and u is newer than the stored report; otherwise the stored report is effective,
//   usable while -future_window <= B - u < T. Guards pay F: no usable report, not one coin in and the other
//   out (LP operations, donations), live D != committed D, edge <= 0, or not corrective (spot moves toward
//   the report without passing it). Parameters 0-3 are b, away capture, toward ratio and F; 4-9 belong to the
//   dual EMA (dual_ema_price_scale.hpp), which drives the price scale on the pool's last_prices; 10-12 are
//   delta, the future window and T.
// Floating-point research only, not a Vyper port.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include "dual_ema_price_scale.hpp"

namespace arb::pools::twocrypto_fx {
template <typename T> struct ChallengeFeePolicy {
    static_assert(std::is_floating_point_v<T>, "the report fee requires floating arithmetic");
    using PriceScale = DualEmaPriceScale<T, 4, 13>;
    using ReportTerms = PolicyReportTerms<T>;
    inline static constexpr PolicyDescriptor<13> DESCRIPTOR{"report_dual_ema", {{
        {"base_fee", 0, "relative", 0.003L, 0.00001L, 0.1L, 0.00001L},
        {"away_capture", 1, "dimensionless", 0.9L, 0.0L, 1.0L, 0.005L},
        {"toward_ratio", 2, "dimensionless", 1.0L, 0.0L, 1.0L, 0.005L},
        {"fallback_fee", 3, "relative", 0.02L, 0.001L, 0.1L, 0.0001L},
        {"fast_half_life_s", 4, "seconds", 600.0L, 60.0L, 604800.0L, 1.0L},
        {"slow_half_life_s", 5, "seconds", 21600.0L, 600.0L, 604800.0L, 1.0L},
        {"kappa", 6, "dimensionless", 1.05L, 0.0L, 2.0L, 0.01L},
        {"deadband", 7, "relative", 0.0L, 0.0L, 0.006L, 0.00001L},
        {"min_cap", 8, "relative", 0.006L, 0.0001L, 0.03L, 0.00001L},
        {"max_cap", 9, "relative", 0.006L, 0.0001L, 0.03L, 0.00001L},
        {"age_offset_s", 10, "seconds", 2.0L, 0.0L, 12.0L, 1.0L},
        {"future_window_s", 11, "seconds", 2.0L, 0.0L, 3.0L, 1.0L},
        {"aging_s", 12, "seconds", 300.0L, 1.0L, 3600.0L, 1.0L},
    }}};
    static constexpr std::size_t PARAM_COUNT = DESCRIPTOR.size();
    static constexpr const char *NAME = DESCRIPTOR.name.data();
    static constexpr double ADMISSION_S = 12;
    static constexpr bool USES_NATIVE_FEE = false, USES_SWAP_REPORTS = true, USES_CACHED_REPORTS = true;

    struct Report {
        T price{T(0)};
        double timestamp{0};  // Original observation time, never the submission time.
    };
    struct State {
        std::array<T, 2> xp{T(0), T(0)};  // committed pre-trade state
        T price_scale{T(0)};
        T D{T(0)};
        Report report{};
        typename PriceScale::State scale{};
        T ratio{1};  // spot_ratio of the committed state, cached by update_state
    };

    static void validate_params(const PolicyConfig<T> &p, const PolicyPoolConfig<T> &c) {
        if (p.n_params != PARAM_COUNT) throw std::invalid_argument("report fee parameter count");
        const auto &a = p.params;
        for (std::size_t i = 0; i < p.n_params; ++i)
            if (!std::isfinite(a[i])) throw std::invalid_argument("nonfinite report fee parameter");
        if (c.precision != T(1) || c.fee_precision != T(1))
            throw std::invalid_argument("report fee needs unit precision");
        if (a[0] < T(.00001) || a[0] > a[3] || a[3] < T(.001) || a[3] > T(.1) || a[1] < 0 || a[1] > 1 ||
            a[2] < 0 || a[2] > 1)
            throw std::invalid_argument("report fee bounds");
        if (a[10] < 0 || a[10] > 12 || a[11] < 0 || a[11] > 3 || a[12] < 1 || a[12] > 3600)
            throw std::invalid_argument("report fee timing bounds");
        // Research bounds on dual-EMA parameters 4-9 (report capture v6), wider than the contract's 60 bp.
        const uint64_t fast = PriceScale::fast_half_life(p), slow = PriceScale::slow_half_life(p);
        if (fast < 60 || slow > 604800 || fast > slow || a[6] < 0 || a[6] > 2 || a[7] < 0 || a[7] > T(.03) ||
            a[8] < T(.0001) || a[8] > a[9] || a[9] > T(.03))
            throw std::invalid_argument("report fee price-scale bounds");
    }

    // Committed pre-trade spot / scale; 1 (no direction) when state is not usable.
    static T spot_ratio(const State &s, const PolicyPoolConfig<T> &c) {
        if (!(s.D > T(0)) || !(s.xp[0] > T(0) && s.xp[1] > T(0))) return T(1);
        return MathOps<T>::get_p(s.xp, s.D, {c.A, T(0)});
    }
    // Delivered report: -future_window <= B - u <= min(12 s, report_max_age_s), finite positive price, newer.
    static bool admissible(const Report &stored, const PolicyConfig<T> &p, const PolicyResearchContext<T> &x) {
        const double age = double(x.block_timestamp) - x.price_feed_timestamp;
        return x.price_feed > T(0) && std::isfinite(x.price_feed) && age >= -double(p.params[11]) &&
               age <= ADMISSION_S && age <= x.report_max_age_s &&
               (!(stored.price > T(0)) || x.price_feed_timestamp > stored.timestamp);
    }
    // Effective report (admissible delivered, else stored) terms for input coin_in; weight 1 when unusable.
    static ReportTerms report_terms(const State &s, const PolicyConfig<T> &p, const PolicyPoolConfig<T> &,
                                    const PolicyResearchContext<T> &x, std::size_t coin_in) {
        const Report r = admissible(s.report, p, x) ? Report{x.price_feed, x.price_feed_timestamp} : s.report;
        const double age = double(x.block_timestamp) - r.timestamp, delta = double(p.params[10]);
        const double aging = double(p.params[12]);
        const bool usable = r.price > T(0) && age >= -double(p.params[11]) && age < aging;
        const bool toward = coin_in == 0 ? s.ratio < T(1) : s.ratio > T(1);
        const T capture = toward ? p.params[1] * p.params[2] : p.params[1];
        return {usable, r.price,
                usable ? T(std::sqrt(std::clamp((age + delta) / (aging + delta), 0.0, 1.0))) : T(1),
                p.params[0], capture, p.params[3]};
    }

    // Whole-trade edge of the swap old -> xp against the report, normalized by the output value.
    static T reported_edge(const State &s, const T &price, const std::array<T, 2> &xp, std::size_t coin_in) {
        const std::size_t coin_out = 1 - coin_in;
        T input_value = xp[coin_in] - s.xp[coin_in];
        T output_value = s.xp[coin_out] - xp[coin_out];
        if (coin_in == 1)
            input_value = input_value * price / s.price_scale;
        else
            output_value = output_value * price / s.price_scale;
        if (!(output_value > input_value)) return T(0);
        return T(1) - input_value / output_value;
    }
    // The swap moves spot toward the report without passing it.
    static bool corrective(const State &s, const PolicyPoolConfig<T> &c, const T &price,
                           const std::array<T, 2> &xp, std::size_t coin_in) {
        if (!(xp[0] > T(0) && xp[1] > T(0))) return false;
        const std::array<T, 2> A_gamma{c.A, T(0)};
        T before = MathOps<T>::get_p(s.xp, s.D, A_gamma);
        T after = MathOps<T>::get_p(xp, s.D, A_gamma);
        if (!(before > T(0) && after > T(0))) return false;
        before = before * s.price_scale;
        after = after * s.price_scale;
        if (!(before > T(0) && after > T(0))) return false;
        return coin_in == 0 ? before < after && after <= price : before > after && after >= price;
    }

    struct Quote {
        const State *state;
        const PolicyPoolConfig<T> *config;
        std::array<ReportTerms, 2> terms;  // by input coin
        T live_D;
        T operator()(const std::array<T, 2> &xp) const {
            const auto &s = *state;
            const auto &old = s.xp;
            const T full = terms[0].fallback;
            if (!terms[0].usable || (xp[0] >= old[0] && xp[1] >= old[1]) || (xp[0] <= old[0] && xp[1] <= old[1]) ||
                !(s.D > T(0)) || !(s.price_scale > T(0)) || !(old[0] > T(0) && old[1] > T(0)) ||
                live_D != s.D)
                return full;
            const std::size_t coin_in = xp[0] > old[0] ? 0 : 1;
            const ReportTerms &t = terms[coin_in];
            const T edge = reported_edge(s, t.price, xp, coin_in);
            if (!(edge > T(0))) return full;
            const T fresh = std::min(full, t.base + t.capture * std::max(T(0), edge - t.base));
            const T fee_rate = (T(1) - t.weight) * fresh + t.weight * full;
            return fee_rate >= full || !corrective(s, *config, t.price, xp, coin_in) ? full : fee_rate;
        }
    };
    // Ephemeral quote: valid only while state, parameters and context are frozen.
    static Quote prepare_fee(const State &s, const PolicyConfig<T> &p, const PolicyPoolConfig<T> &c,
                             const PolicyResearchContext<T> &x, const T &live_D) {
        return {&s, &c, {{report_terms(s, p, c, x, 0), report_terms(s, p, c, x, 1)}}, live_D};
    }
    static T get_fee(const State &s, const PolicyConfig<T> &p, const PolicyPoolConfig<T> &c,
                     const PolicyResearchContext<T> &x, const std::array<T, 2> &xp, const T &live_D) {
        return prepare_fee(s, p, c, x, live_D)(xp);
    }
    static T fee_floor(const PolicyConfig<T> &p, const PolicyPoolConfig<T> &, const T &) {
        return p.params[0];
    }
    // Every charged fee is >= (1 - w) * b + w * F (F without a usable report).
    static T context_fee_floor(const State &s, const PolicyConfig<T> &p, const PolicyPoolConfig<T> &c,
                               const PolicyResearchContext<T> &x, std::size_t coin_in) {
        const ReportTerms t = report_terms(s, p, c, x, coin_in);
        return (T(1) - t.weight) * t.base + t.weight * t.fallback;
    }
    // Profit at trade edge e (e <= spot edge < 1 for every size) needs h(e) = 1 - rho (1 - e) - fee(e) > 0, and
    // guards only raise the fee to F. h rises except on (b, k), k = b + (F - b) / c, where it falls only if
    // rho < c (1 - w); then h > 0 on [b, 1) (h(k) > (1 - c)(1 - b) if k < 1, h(1) >= 1 - F). So h(spot edge)
    // decides the sign for every size.
    static bool context_may_profit(const State &s, const PolicyConfig<T> &p, const PolicyPoolConfig<T> &c,
                                   const PolicyResearchContext<T> &x, std::size_t i, T spot, T external) {
        if (!(spot > T(0)) || !(external > T(0)) || !std::isfinite(spot) || !std::isfinite(external))
            return true;
        const ReportTerms t = report_terms(s, p, c, x, i);
        const T full = t.fallback, slack = T(1e-9);
        const T edge = !t.usable ? T(0) : i == 0 ? T(1) - spot / t.price : T(1) - t.price / spot;
        if (!(edge > T(0)))  // no usable report, or no size has a positive report edge: all pay F
            return i == 0 ? (T(1) - full) * external > spot * (T(1) - slack)
                          : (T(1) - full) * spot > external * (T(1) - slack);
        const T rho = i == 0 ? t.price / external : external / t.price;
        const T fresh = std::min(full, t.base + t.capture * std::max(T(0), edge - t.base));
        return T(1) - rho * (T(1) - edge) - ((T(1) - t.weight) * fresh + t.weight * full) > -slack;
    }

    static T get_price_scale(State &s, PolicyResearchContext<T> &x, const PolicyConfig<T> &p,
                             const PolicyPoolConfig<T> &c) {
        return PriceScale::get_price_scale(s.scale, x, p, c);
    }
    static void update_state(State &s, PolicyResearchContext<T> &x, const PolicyConfig<T> &p,
                             const PolicyPoolConfig<T> &c, const PolicyUpdate<T> &u) {
        if (admissible(s.report, p, x)) s.report = {x.price_feed, x.price_feed_timestamp};
        s.xp = u.xp;
        s.price_scale = u.price_scale;
        s.D = u.D;
        s.ratio = spot_ratio(s, c);
        PriceScale::update_state(s.scale, x, p, c, u);
    }
};
} // namespace arb::pools::twocrypto_fx
