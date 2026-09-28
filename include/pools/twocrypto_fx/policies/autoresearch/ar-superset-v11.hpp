// Autoresearch superset v11 = v10 + launch warm-up fee (Opus, 2026-09-27).
//   p53 warm-up seconds (0 disables), p54 base multiplier at launch (>= 1), p55 capture delta at launch (>= 0).
//   With age = now - first committed update and w = 1 - age / p53 (while age < p53), the effective base is
//   min(fallback, base * (1 + (p54 - 1) * w)) and capture + p55 * w (clamped to 1). Fees only rise, so every
//   lower bound (global floor, context floors) stays valid; the global floor is unchanged (no sizing confound).
// v10 header text follows.
// Autoresearch superset v10 = v9 + exact-rejection adaptive step (Opus, 2026-09-26).
//   p52 = 1: the step fraction (p37 shrink, p38 growth, p39 floor) reacts only to the policy's OWN outcome:
//   get_price_scale records whether its target moves the actuator preview; at the next committed update the
//   request is a rejection if the committed scale did not move (shrink), a success if it moved (grow).
//   Calls where the pool never asked (readiness gate closed) leave the fraction unchanged. p52 = 0 keeps the
//   v2 inference. Causal: the policy stores its own last request and reads the committed scale.
// v9 header text follows.
// Autoresearch superset v9 = v8 + detachment surcharge on away trades (Opus, 2026-09-26).
//   With the recovery weight w (p44/p45), swaps that initially move spot AWAY from the committed scale get
//   capture c + p42 + p50*w (clamped [0,1]) and base min(fallback, b * (1 + (p51 - 1) * w)), p50 in [0,1],
//   p51 in [1, 10]. Fees only rise, so every lower bound is unchanged. Requires p45 > 0 to act.
// v8 header text follows.
// Autoresearch superset v8 = v7 + one-sided signal ramp (Opus, 2026-09-26).
//   p49 = 1: signal-scaled fees use max(0, z - z_ref), so fees only rise above the reference (quiet-period
//   fees stay at the anchors); p49 = 0 is v7 (linear in z - z_ref).
// v7 header text follows.
// Autoresearch superset v7 = v5 + detachment-conditional recovery discount (Opus, 2026-09-26).
//   Detachment g = committed |log(fresh report / committed scale)| (native spot gap without a fresh
//   report). Weight w = clamp((g - p44) / (p45 - p44), 0, 1); p44 = 0 and p45 = 0 disables.
//   Swaps that move spot TOWARD the committed scale (the recovery direction) get capture
//   c + p36 + p46*w (clamped [0,1]) and base fee max(.001, b * (1 - (1 - p47) * w)), p47 in [.1, 1].
//   Every quote bound uses the lowest reachable capture and base (w = 1).
//   p48 signal reference z_ref: signal-scaled fees use (z - z_ref), so base/fallback/capture equal their
//   anchors at z = z_ref (LP operations keep the anchors). The global sizing floor is exact: the base
//   anchor when p13 = 0, else its lowest reachable value for nonnegative signals, else 10 bp.
// v5 header text follows.
// Autoresearch superset v5 = v4 + report-latency grace (Opus, 2026-09-26).
//   p43 grace g seconds in [0, 10]: fee quotes/bounds see report age max(0, age - g) (strictly
//   monotone map, so report selection and ordering are unchanged), aging horizon p3 - g and
//   admission max(0, 12 - g): aging becomes sqrt((age - g)/(T - g)). Commits are unchanged.
// v4 header text follows.
// Autoresearch superset v4 = v3 + stress fee ceilings up to 10% (Opus, 2026-09-26).
//   p15/p16 base/fallback ceilings may reach 0.10 (v3: 0.025), so signal-scaled fees can capture
//   large reported edges in crashes/rallies. LP operations use get_liquidity_fee: the static
//   (signal-free, direction-free) parameters, so YB add/remove legs never pay stress fees.
// v3 header text follows.
// Autoresearch superset v3 = v2 + hybrid EMA source and away-from-scale capture (Opus, 2026-09-26).
//   p19 = 2: hybrid source: the dual EMA observes the committed fresh report only while
//       |log(report/scale)| > p41, otherwise pool last_prices (p19 0/1 unchanged).
//   p42 away capture delta: capture + p42 (clamped [0,1]) for swaps moving spot AWAY from the
//       committed scale; 0 disables. Bounds use min(capture, capture+p36, capture+p42).
// v2 header text follows.
// Autoresearch superset v2 = v1 + committed-outcome step backoff (Opus, 2026-09-26).
//   p37 backoff multiplier in (0,1) (0 disables), p38 recovery multiplier >= 1, p39 min fraction.
//   The requested scale move (target - current) is scaled by fraction f. At each committed
//   update, if the committed scale moved f = min(1, f*p38); else if the pre-update state would
//   have requested a move at this timestamp f = max(p39, f*p37). Inference from committed state
//   only (cannot tell profit-not-ready from gate failure; both indicate budget shortage).
//   p40 cap scale in [1, 50]: multiplies min/max policy caps (p9/p10) after validation, so
//   per-call moves may exceed the 60bp DualEMA bound (research actuator; pool clamps still apply).
// v1 header text follows.
// Autoresearch superset v1 (Opus, 2026-09-26); floating-point research only, not a Vyper port.
// p0..30 are exactly adaptive-capture-gap-report-driver-20260925.hpp (capture gain p27..29,
// signal 9 = committed native spot/scale gap above p30). p31..34 are the reversal-hysteresis
// actuator slots (p27..30 there): p31 actuator mode 0 capped / 1 velocity / 2 exponential,
// p32 velocity per hour, p33 approach half-life s, p34 reversal gap. New opt-in mechanisms:
//   p35 quiet report band: hold price_scale while the COMMITTED fresh report is within p35
//       (relative log gap) of the committed scale; 0 disables.
//   p36 directional capture delta: capture + p36 (clamped to [0,1]) for swaps that move spot
//       toward the committed scale; 0 disables. Bounds use min(capture, capture + p36).
//   signal 10: committed fresh-report/scale log gap above p30; native gap when no fresh report.
// All new inputs are committed state; quotes never see the report they propose.
#pragma once
#include <memory>
#include "../dual_ema_price_scale.hpp"
#include "../shaped_oraclized_fair_fee.hpp"
#include "pools/twocrypto_fx/price_scale_actuator.hpp"

namespace arb::pools::twocrypto_fx {
template <typename T> struct ChallengeFeePolicy {
    static_assert(std::is_floating_point_v<T>, "dynamic fee research requires floating arithmetic");
    using Fee = ShapedOraclizedFairFeePolicy<T, 20, 21>;
    using PriceScale = DualEmaPriceScale<T, 5, 56>;
    inline static constexpr auto DESCRIPTOR = [] {
        PolicyDescriptor<56> d{"ar_superset_v11_report_driver_dual_ema", {}};
        for (std::size_t i = 0; i < 4; ++i)
            d.parameters[i] = Fee::Base::DESCRIPTOR.parameters[i];
        d.parameters[4] = {"max_report_age_s", 4, "seconds", 1, 0.001, 12, 0.001};
        for (std::size_t i = 0; i < 6; ++i) {
            d.parameters[i + 5] = PriceScale::DESCRIPTOR.parameters[i];
            d.parameters[i + 5].order = i + 5;
        }
        d.parameters[11] = {"fee_signal", 11, "enum", 0, 0, 10, 1};
        d.parameters[12] = {"signal_half_life_s", 12, "seconds", 3600, 12, 604800, 1};
        d.parameters[13] = {"base_signal_gain", 13, "dimensionless", 0, -1000, 1000, 0.1};
        d.parameters[14] = {"fallback_signal_gain", 14, "dimensionless", 0, -1000, 1000, 0.1};
        d.parameters[15] = {"base_fee_ceiling", 15, "relative", 0.005, 0.001, 0.1, 0.0001};
        d.parameters[16] = {"fallback_fee_ceiling", 16, "relative", 0.025, 0.001, 0.1, 0.0001};
        d.parameters[17] = {"signal_block_seconds", 17, "seconds", 12, 1, 60, 1};
        d.parameters[18] = {"prior_block_sigma", 18, "relative", 0.00032, 0, 0.01, 0.00001};
        d.parameters[19] = {"ema_source", 19, "enum", 0, 0, 2, 1};
        d.parameters[20] = {"shape_mode", 20, "enum", 0, 0, 3, 1};
        d.parameters[21] = {"young_hedge_allowance", 21, "relative", 0, 0, 0.001, 0.00001};
        d.parameters[22] = {"ema_vol_exponent", 22, "dimensionless", 0, 0, 2, 0.5};
        d.parameters[23] = {"ema_reference_sigma", 23, "relative", 0.00032, 0.00001, 0.01, 0.00001};
        d.parameters[24] = {"ema_min_time_multiplier", 24, "dimensionless", 0.25, 0.05, 1, 0.05};
        d.parameters[25] = {"ema_max_time_multiplier", 25, "dimensionless", 4, 1, 32, 1};
        d.parameters[26] = {"ema_clock_scope", 26, "enum", 0, 0, 1, 1};
        d.parameters[27] = {"capture_signal_gain", 27, "dimensionless", 0, -1000, 1000, 1};
        d.parameters[28] = {"capture_floor", 28, "relative", 0, 0, 1, 0.01};
        d.parameters[29] = {"capture_ceiling", 29, "relative", 1, 0, 1, 0.01};
        d.parameters[30] = {"gap_signal_deadband", 30, "relative", 0.01, 0, 0.2, 0.001};
        d.parameters[31] = {"actuator_mode", 31, "enum", 0, 0, 2, 1};
        d.parameters[32] = {"max_relative_scale_velocity_per_hour", 32, "relative", 0.06, 0.001, 1, 0.001};
        d.parameters[33] = {"gap_approach_half_life_s", 33, "seconds", 600, 60, 86400, 1};
        d.parameters[34] = {"reversal_gap", 34, "relative", 0, 0, 0.1, 0.001};
        d.parameters[35] = {"quiet_report_band", 35, "relative", 0, 0, 0.1, 0.0005};
        d.parameters[36] = {"toward_capture_delta", 36, "dimensionless", 0, -1, 1, 0.05};
        d.parameters[37] = {"step_backoff", 37, "dimensionless", 0, 0, 0.99, 0.01};
        d.parameters[38] = {"step_recovery", 38, "dimensionless", 2, 1, 16, 0.1};
        d.parameters[39] = {"step_min_fraction", 39, "dimensionless", 0.0625, 0.001, 1, 0.001};
        d.parameters[40] = {"cap_scale", 40, "dimensionless", 1, 1, 50, 0.5};
        d.parameters[41] = {"hybrid_source_gap", 41, "relative", 0.01, 0, 0.2, 0.001};
        d.parameters[42] = {"away_capture_delta", 42, "dimensionless", 0, -1, 1, 0.05};
        d.parameters[43] = {"report_age_grace_s", 43, "seconds", 0, 0, 10, 0.5};
        d.parameters[44] = {"recovery_gap_start", 44, "relative", 0, 0, 0.5, 0.001};
        d.parameters[45] = {"recovery_gap_full", 45, "relative", 0, 0, 0.5, 0.001};
        d.parameters[46] = {"recovery_capture_delta", 46, "dimensionless", 0, -1, 0, 0.05};
        d.parameters[47] = {"recovery_base_multiplier", 47, "dimensionless", 1, 0.1, 1, 0.05};
        d.parameters[48] = {"signal_reference", 48, "relative", 0, 0, 0.5, 0.00001};
        d.parameters[49] = {"signal_one_sided", 49, "enum", 0, 0, 1, 1};
        d.parameters[50] = {"away_surcharge_capture_delta", 50, "dimensionless", 0, 0, 1, 0.05};
        d.parameters[51] = {"away_surcharge_base_multiplier", 51, "dimensionless", 1, 1, 10, 0.1};
        d.parameters[52] = {"step_outcome_signal", 52, "enum", 0, 0, 1, 1};
        d.parameters[53] = {"warmup_seconds", 53, "seconds", 0, 0, 10000000, 1};
        d.parameters[54] = {"warmup_base_multiplier", 54, "dimensionless", 1, 1, 5, 0.05};
        d.parameters[55] = {"warmup_capture_delta", 55, "dimensionless", 0, 0, 1, 0.01};
        return d;
    }();
    static constexpr std::size_t PARAM_COUNT = DESCRIPTOR.size();
    static constexpr const char *NAME = DESCRIPTOR.name.data();
    static constexpr bool USES_NATIVE_FEE = false, USES_SWAP_REPORTS = true,
                          USES_CACHED_REPORTS = true;
    struct State {
        typename Fee::State fee{};
        typename PriceScale::State scale{};
        bool initialized{false};
        uint64_t block{0};
        T variance{0}, absolute_return{0}, daily_profit{0}; // Before pending block.
        T start_price{0}, end_price{0}, start_profit{0}, end_profit{0};
        T report_variance{0}, report_price{0};
        double report_ts{0};
        int last_scale_direction{0};
        T step_fraction{1};
        bool move_requested{false};
        uint64_t launch_ts{0};  // first committed update (warm-up clock)  // decision state: last target moved the actuator preview (p52 = 1)
        // Research-only counters; never consulted by policy decisions.
        uint64_t target_calls{0}, actuator_holds{0}, gate_rejections{0};
        bool diagnostic_move_pending{false};
    };
    static void validate_params(const PolicyConfig<T> &p, const PolicyPoolConfig<T> &c) {
        if (p.n_params != PARAM_COUNT)
            throw std::invalid_argument("dynamic fee parameter count");
        auto fixed = p;
        fixed.n_params = 11;
        Fee::validate_params(fixed, c);
        PriceScale::validate_params(p, c);
        for (std::size_t i = 11; i < PARAM_COUNT; ++i)
            if (!std::isfinite(p.params[i]))
                throw std::invalid_argument("nonfinite fee controller parameter");
        const auto &a = p.params;
        if (a[19] != T(0) && a[19] != T(1) && a[19] != T(2)) throw std::invalid_argument("ema source");
        if (a[27] < -1000 || a[27] > 1000 || a[28] < 0 || a[29] > 1 ||
            a[28] > a[29] || a[1] < a[28] || a[1] > a[29] || a[30] < 0 || a[30] > T(.2))
            throw std::invalid_argument("adaptive capture bounds");
        if (a[22] < 0 || a[22] > 2 || a[23] < T(.00001) || a[23] > T(.01) ||
            a[24] < T(.05) || a[24] > T(1) || a[25] < T(1) || a[25] > T(32) ||
            (a[26] != T(0) && a[26] != T(1)))
            throw std::invalid_argument("volatility clock bounds");
        if ((a[31] != T(0) && a[31] != T(1) && a[31] != T(2)) || a[32] < T(.001) || a[32] > T(1) ||
            a[33] < T(60) || a[33] > T(86400) || std::floor(a[33]) != a[33] ||
            a[34] < T(0) || a[34] > T(.1))
            throw std::invalid_argument("actuator bounds");
        if (a[35] < T(0) || a[35] > T(.1) || a[36] < T(-1) || a[36] > T(1) ||
            a[37] < T(0) || a[37] > T(.99) || a[38] < T(1) || a[38] > T(16) || a[39] < T(.001) || a[39] > T(1) ||
            a[40] < T(1) || a[40] > T(50) || a[41] < T(0) || a[41] > T(.2) || a[42] < T(-1) || a[42] > T(1) ||
            a[43] < T(0) || a[43] > T(10) || a[44] < T(0) || a[45] > T(.5) || a[44] > a[45] ||
            (a[45] > T(0) && !(a[45] > a[44])) || a[46] < T(-1) || a[46] > T(0) || a[47] < T(.1) || a[47] > T(1) ||
            a[48] < T(0) || a[48] > T(.5) || (a[49] != T(0) && a[49] != T(1)) ||
            a[50] < T(0) || a[50] > T(1) || a[51] < T(1) || a[51] > T(10) || (a[52] != T(0) && a[52] != T(1)) ||
            a[53] < T(0) || a[53] > T(10000000) || a[54] < T(1) || a[54] > T(5) || a[55] < T(0) || a[55] > T(1))
            throw std::invalid_argument("autoresearch bounds");
        if (a[11] < 0 || a[11] > 10 || std::floor(a[11]) != a[11] || a[12] < 12 || a[12] > 604800 ||
            a[13] < -1000 || a[13] > 1000 || a[14] < -1000 || a[14] > 1000 || a[0] < T(.001) ||
            a[0] > a[15] || a[15] > a[16] || a[2] > a[16] || a[16] > T(.1) || a[17] < 1 ||
            a[17] > 60 || std::floor(a[17]) != a[17] || a[18] < 0 || a[18] > .01 || 
            (a[20] != 0 && a[20] != 1 && a[20] != 2 && a[20] != 3) ||
            a[21] < 0 || a[21] > T(.001) || (a[21] > 0 && a[20] != T(3)))
            throw std::invalid_argument("dynamic fee controller bounds");
    }
    struct Moments {
        T variance, absolute_return, daily_profit;
    };
    static Moments moments(const State &s, const PolicyConfig<T> &p, uint64_t now) {
        if (!s.initialized)
            return {p.params[18] * p.params[18], p.params[18] * T(.7978845608028654), 0};
        const auto block = now / static_cast<uint64_t>(p.params[17]);
        if (block < s.block)
            throw std::underflow_error("fee signal timestamp");
        if (block == s.block)
            return {s.variance, s.absolute_return, s.daily_profit};
        const T lambda = std::exp(-T(.693147180559945309L) * p.params[17] / p.params[12]);
        const T decay = std::exp(-T(.693147180559945309L) * T(block - s.block - 1) * p.params[17] /
                                 p.params[12]);
        const T change = std::log(s.end_price / s.start_price);
        const T profit = std::log(s.end_profit / s.start_profit) * T(86400) / p.params[17];
        return {(lambda * s.variance + (1 - lambda) * change * change) * decay,
                (lambda * s.absolute_return + (1 - lambda) * std::abs(change)) * decay,
                (lambda * s.daily_profit + (1 - lambda) * profit) * decay};
    }
    // Committed fresh report (age < aging horizon p3) or zero.
    static T fresh_report(const State &s, const PolicyConfig<T> &p, uint64_t now) {
        const auto &report = s.fee.base.report;
        const double age = double(now) - report.timestamp;
        return report.price > T(0) && std::isfinite(age) && age >= 0 && age < double(p.params[3])
            ? report.price : T(0);
    }
    static T signal(const State &s, const PolicyConfig<T> &p, uint64_t now) {
        const int mode = static_cast<int>(p.params[11]);
        if (mode == 0)
            return 0;
        if (mode == 9 || mode == 10) {
            const T scale = s.fee.base.price_scale;
            if (!(scale > T(0))) return T(0);
            const T report = mode == 10 ? fresh_report(s, p, now) : T(0);
            if (report > T(0)) return std::max(T(0), std::abs(std::log(report / scale)) - p.params[30]);
            if (!s.initialized || !(s.end_price > T(0))) return T(0);
            return std::max(T(0), std::abs(std::log(s.end_price / scale)) - p.params[30]);
        }
        if (mode == 7 || mode == 8) {
            if (!(s.report_price > 0)) return mode == 8 ? T(0) : p.params[18];
            const T age = T(std::max(0.0, double(now) - s.report_ts));
            const T decay = std::exp(-T(.693147180559945309L) * age / p.params[12]);
            const T sigma = std::sqrt(std::max(T(0), s.report_variance * decay));
            if (mode == 7) return sigma;
            const T scale = s.fee.base.price_scale;
            return s.report_price > scale ? sigma : (s.report_price < scale ? -sigma : T(0));
        }
        if (mode == 3) {
            if (!s.initialized)
                return 0;
            const T fast = PriceScale::get_emas(s.scale, now, p)[0];
            const T close =
                now / static_cast<uint64_t>(p.params[17]) == s.block ? s.start_price : s.end_price;
            return fast > 0 ? std::abs(std::log(close / fast)) : T(0);
        }
        const auto m = moments(s, p, now);
        if (mode == 1)
            return std::sqrt(std::max(T(0), m.variance));
        if (mode == 2)
            return std::max(T(0), m.absolute_return);
        if (mode == 5)
            return -std::max(T(0), m.daily_profit);
        if (mode == 6)
            return -std::sqrt(std::max(T(0), m.variance));
        return std::max(T(0), m.daily_profit);
    }
    static PolicyConfig<T> effective_params(const State &s, const PolicyConfig<T> &p,
                                            uint64_t now) {
        auto out = p;
        out.n_params = 11;
        const T z = p.params[49] == T(1) ? std::max(T(0), signal(s, p, now) - p.params[48])
                                         : signal(s, p, now) - p.params[48];
        if (p.params[27] != T(0))
            out.params[1] = std::clamp(p.params[1] + p.params[27] * z,
                                        p.params[28], p.params[29]);
        out.params[0] = std::max(T(.001), std::min(p.params[15], p.params[0] + p.params[13] * z));
        out.params[2] =
            std::max(out.params[0], std::min(p.params[16], p.params[2] + p.params[14] * z));
        if (p.params[53] > T(0) && s.launch_ts > 0 && now >= s.launch_ts && T(now - s.launch_ts) < p.params[53]) {
            const T w = T(1) - T(now - s.launch_ts) / p.params[53];
            out.params[0] = std::min(out.params[2], out.params[0] * (T(1) + (p.params[54] - T(1)) * w));
            out.params[1] = std::clamp(out.params[1] + p.params[55] * w, T(0), T(1));
        }
        return out;
    }
    static bool recovery_on(const PolicyConfig<T> &p) { return p.params[45] > T(0); }
    // Committed detachment weight in [0, 1] (0 when recovery is disabled or the scale is unset).
    static T recovery_weight(const State &s, const PolicyConfig<T> &p, uint64_t now) {
        if (!recovery_on(p)) return T(0);
        const T scale = s.fee.base.price_scale;
        if (!(scale > T(0))) return T(0);
        T ref = fresh_report(s, p, now);
        if (!(ref > T(0))) ref = s.initialized ? s.end_price : T(0);
        if (!(ref > T(0))) return T(0);
        const T g = std::abs(std::log(ref / scale));
        return std::clamp((g - p.params[44]) / (p.params[45] - p.params[44]), T(0), T(1));
    }
    // Capture (and recovery base) for swaps moving spot toward the committed scale.
    static PolicyConfig<T> toward_params(PolicyConfig<T> e, const PolicyConfig<T> &p, T w = T(0)) {
        e.params[1] = std::clamp(e.params[1] + p.params[36] + p.params[46] * w, T(0), T(1));
        if (w > T(0)) e.params[0] = std::max(T(.001), e.params[0] * (T(1) - (T(1) - p.params[47]) * w));
        return e;
    }
    static bool surcharge_on(const PolicyConfig<T> &p) { return recovery_on(p) && (p.params[50] > T(0) || p.params[51] > T(1)); }
    static PolicyConfig<T> away_params(PolicyConfig<T> e, const PolicyConfig<T> &p, T w = T(0)) {
        e.params[1] = std::clamp(e.params[1] + p.params[42] + p.params[50] * w, T(0), T(1));
        if (w > T(0) && p.params[51] > T(1))
            e.params[0] = std::min(e.params[2], e.params[0] * (T(1) + (p.params[51] - T(1)) * w));
        return e;
    }
    static PolicyConfig<T> bound_params(const State &s, const PolicyConfig<T> &p, uint64_t now) {
        auto e = effective_params(s, p, now);
        const T toward_low = p.params[36] + (recovery_on(p) ? std::min(T(0), p.params[46]) : T(0));
        const T lowest = std::min({T(0), toward_low, p.params[42]});
        if (lowest < T(0)) e.params[1] = std::clamp(e.params[1] + lowest, T(0), T(1));
        if (recovery_on(p) && p.params[47] < T(1)) e.params[0] = std::max(T(.001), e.params[0] * p.params[47]);
        return e;
    }
    // Committed pre-trade spot / scale; 1 (no direction) when state is not usable.
    static T spot_ratio(const State &s, const PolicyPoolConfig<T> &c) {
        const auto &old = s.fee.base.xp;
        if (!(s.fee.D > T(0)) || !(old[0] > T(0) && old[1] > T(0))) return T(1);
        return MathOps<T>::get_p(old, s.fee.D, {c.A, T(0)}) / c.precision;
    }
    static bool toward(T ratio, const std::array<T, 2> &old, const std::array<T, 2> &xp) {
        return xp[0] > old[0] ? ratio < T(1) : (xp[1] > old[1] ? ratio > T(1) : false);
    }
    static bool away(T ratio, const std::array<T, 2> &old, const std::array<T, 2> &xp) {
        return xp[0] > old[0] ? ratio > T(1) : (xp[1] > old[1] ? ratio < T(1) : false);
    }
    static std::array<T, 3> fee_diagnostics(const State &s, const PolicyConfig<T> &p,
                                            uint64_t now) {
        const auto effective = effective_params(s, p, now);
        return {effective.params[0], effective.params[2], signal(s, p, now)};
    }
    // Quote-side report-latency grace: shift report timestamps by a strictly monotone map.
    static double graced_ts(double now, double ts, double g) {
        const double age = now - ts;
        if (!(age > 0) || !std::isfinite(age)) return ts;
        return now - std::max(0.0, age - g) - 1e-4 * age;  // 1e-4 s per s: above double ulp at unix times
    }
    static void apply_grace(const PolicyConfig<T> &p, typename Fee::State &fs, PolicyResearchContext<T> &x,
                            PolicyConfig<T> &e) {
        const double g = double(p.params[43]);
        if (!(g > 0)) return;
        const double now = double(x.block_timestamp);
        if (fs.base.report.price > T(0)) fs.base.report.timestamp = graced_ts(now, fs.base.report.timestamp, g);
        if (x.price_feed > T(0)) x.price_feed_timestamp = graced_ts(now, x.price_feed_timestamp, g);
        const double max_age = std::min(x.report_max_age_s, double(p.params[4]));
        x.report_max_age_s = std::max(0.0, max_age - g) + 1e-4 * max_age;
        e.params[3] = std::max(T(1), p.params[3] - T(g));
    }
    static T get_fee(const State &s, const PolicyConfig<T> &p, const PolicyPoolConfig<T> &c,
                     const PolicyResearchContext<T> &x0, const std::array<T, 2> &xp, const T &D) {
        auto e = effective_params(s, p, x0.block_timestamp);
        auto fs = s.fee; auto x = x0;
        apply_grace(p, fs, x, e);
        if (p.params[36] != T(0) || p.params[42] != T(0) || recovery_on(p) || surcharge_on(p)) {
            const T ratio = spot_ratio(s, c);
            const T w = recovery_weight(s, p, x0.block_timestamp);
            if ((p.params[36] != T(0) || w > T(0)) && toward(ratio, s.fee.base.xp, xp)) e = toward_params(e, p, w);
            else if ((p.params[42] != T(0) || (surcharge_on(p) && w > T(0))) && away(ratio, s.fee.base.xp, xp)) e = away_params(e, p, w);
        }
        return Fee::get_fee(fs, e, c, x, xp, D);
    }
    // LP operations: static parameters only (no signal, no directional delta).
    static T get_liquidity_fee(const State &s, const PolicyConfig<T> &p, const PolicyPoolConfig<T> &c,
                               const PolicyResearchContext<T> &x, const std::array<T, 2> &xp, const T &D) {
        auto q = p;
        q.n_params = 11;
        return Fee::get_fee(s.fee, q, c, x, xp, D);
    }
    static auto prepare_fee(const State &s, const PolicyConfig<T> &p, const PolicyPoolConfig<T> &c,
                            const PolicyResearchContext<T> &x0, const T &D) {
        auto e = effective_params(s, p, x0.block_timestamp);
        // Fee closures hold references to the fee state: keep the graced copy alive with them.
        auto held = std::make_shared<typename Fee::State>(s.fee);
        auto x = x0;
        apply_grace(p, *held, x, e);
        const T w = recovery_weight(s, p, x0.block_timestamp);
        const bool to = p.params[36] != T(0) || w > T(0), aw = p.params[42] != T(0) || (surcharge_on(p) && w > T(0));
        auto normal = Fee::prepare_fee(*held, e, c, x, D);
        auto closer = to ? Fee::prepare_fee(*held, toward_params(e, p, w), c, x, D) : normal;
        auto farther = aw ? Fee::prepare_fee(*held, away_params(e, p, w), c, x, D) : normal;
        const T ratio = to || aw ? spot_ratio(s, c) : T(1);
        const std::array<T, 2> old = s.fee.base.xp;
        return [held, normal, closer, farther, to, aw, ratio, old](const std::array<T, 2> &xp) -> T {
            if (to && toward(ratio, old, xp)) return closer(xp);
            if (aw && away(ratio, old, xp)) return farther(xp);
            return normal(xp);
        };
    }
    static T fee_floor(const PolicyConfig<T> &p, const PolicyPoolConfig<T> &, const T &) {
        if (recovery_on(p) && p.params[47] < T(1)) return T(.001);
        const int mode = static_cast<int>(p.params[11]);
        const bool nonnegative = mode != 5 && mode != 6 && mode != 8;
        if (p.params[13] == T(0) || (p.params[13] > T(0) && p.params[49] == T(1))) return p.params[0];
        if (p.params[13] > T(0) && nonnegative)
            return std::max(T(.001), std::min(p.params[15], p.params[0] - p.params[13] * p.params[48]));
        return T(.001);
    }
    static T context_fee_floor(const State &s, const PolicyConfig<T> &p,
                               const PolicyPoolConfig<T> &c, const PolicyResearchContext<T> &x,
                               std::size_t i) {
        auto e = bound_params(s, p, x.block_timestamp); auto fs = s.fee; auto xg = x;
        apply_grace(p, fs, xg, e);
        return Fee::context_fee_floor(fs, e, c, xg, i);
    }
    static bool context_may_profit(const State &s, const PolicyConfig<T> &p,
                                   const PolicyPoolConfig<T> &c, const PolicyResearchContext<T> &x,
                                   std::size_t i, T spot, T external) {
        auto e = bound_params(s, p, x.block_timestamp); auto fs = s.fee; auto xg = x;
        apply_grace(p, fs, xg, e);
        return Fee::context_may_profit(fs, e, c, xg, i, spot, external);
    }
    static PolicyConfig<T> price_params(const State &s, const PolicyConfig<T> &p) {
        if (p.params[40] != T(1)) {  // Scaled caps: applied after validation only.
            auto scaled = p;
            scaled.params[40] = T(1);
            scaled.params[9] *= p.params[40];
            scaled.params[10] *= p.params[40];
            return price_params(s, scaled);
        }
        if (p.params[22] == T(0)) return p; // Exact fixed-clock control.
        auto out = p;
        const T variance = s.report_price > T(0) ? s.report_variance : p.params[18] * p.params[18];
        const T sigma = std::max(std::sqrt(std::max(T(0), variance)), p.params[23] * T(1e-6));
        const T factor = std::clamp(std::pow(p.params[23] / sigma, p.params[22]),
                                    p.params[24], p.params[25]);
        for (std::size_t i : {std::size_t(5), std::size_t(6)})
            out.params[i] = std::clamp(std::round(p.params[i] * factor), T(600), T(604800));
        if (p.params[26] == T(1)) out.params[6] = p.params[6];
        out.params[5] = std::min(out.params[5], out.params[6]);
        return out;
    }
    static T price_target(State &s, PolicyResearchContext<T> &x, const PolicyConfig<T> &p,
                          const PolicyPoolConfig<T> &c) {
        const auto q = price_params(s, p);
        const T current = s.scale.price_scale;
        if (p.params[35] > T(0) && current > T(0)) {
            const T report = fresh_report(s, p, x.block_timestamp);
            if (report > T(0) && std::abs(std::log(report / current)) < p.params[35]) return current;
        }
        if (p.params[34] > T(0) && s.last_scale_direction != 0 && current > T(0)) {
            const T gap = PriceScale::raw_target(s.scale, x.block_timestamp, q) - current;
            if (T(s.last_scale_direction) * gap < T(0) &&
                std::abs(gap) <= current * p.params[34]) return current;
        }
        if (p.params[31] == T(0)) return PriceScale::get_price_scale(s.scale, x, q, c);
        if (current == T(0)) return T(0);
        const uint64_t now = x.block_timestamp;
        if (now < s.scale.last_update_ts) throw std::underflow_error("policy timestamp");
        const uint64_t dt = now - s.scale.last_update_ts;
        if (dt == 0) return current;
        const T target = PriceScale::raw_target(s.scale, now, q);
        const T gap = target >= current ? target - current : current - target;
        if (gap <= current * PriceScale::deadband(q)) return current;
        T desired = std::min(gap, current * PriceScale::max_cap(q));
        if (p.params[31] == T(1))
            desired = std::min(desired, current * p.params[32] * T(dt) / T(3600));
        else
            desired = std::min(desired, gap * -std::expm1(-T(.693147180559945309L) * T(dt) / p.params[33]));
        const T target_gap = T(5) * desired;  // Native actuator moves by desired.
        return target >= current ? current + target_gap : current - target_gap;
    }
    static T scaled_target(State &s, PolicyResearchContext<T> &x, const PolicyConfig<T> &p,
                           const PolicyPoolConfig<T> &c) {
        const T target = price_target(s, x, p, c);
        const T current = s.scale.price_scale;
        if (p.params[37] == T(0) || !(current > T(0)) || s.step_fraction >= T(1)) return target;
        return current + s.step_fraction * (target - current);
    }
    static T get_price_scale(State &s, PolicyResearchContext<T> &x, const PolicyConfig<T> &p,
                             const PolicyPoolConfig<T> &c) {
        const T target = scaled_target(s, x, p, c);
        if (!(s.scale.price_scale > T(0))) return target;
        const auto preview = preview_price_scale_actuator(target, x.price_oracle,
            s.scale.price_scale, c.precision, c.adjustment_step_min, c.adjustment_step_max);
        ++s.target_calls;
        s.diagnostic_move_pending = preview.p_new != s.scale.price_scale;
        s.move_requested = s.diagnostic_move_pending;
        s.actuator_holds += !s.diagnostic_move_pending;
        return target;
    }
    static std::array<uint64_t, 3> price_diagnostics(const State& s) {
        return {s.target_calls, s.actuator_holds, s.gate_rejections};
    }
    static void update_state(State &s, PolicyResearchContext<T> &x, const PolicyConfig<T> &p,
                             const PolicyPoolConfig<T> &c, const PolicyUpdate<T> &u) {
        if (s.diagnostic_move_pending && u.price_scale == s.scale.price_scale)
            ++s.gate_rejections;
        s.diagnostic_move_pending = false;
        const bool requested = s.move_requested;
        s.move_requested = false;
        if (p.params[37] > T(0) && p.params[52] == T(1) && s.scale.price_scale > T(0)) {
            if (u.price_scale != s.scale.price_scale) s.step_fraction = std::min(T(1), s.step_fraction * p.params[38]);
            else if (requested) s.step_fraction = std::max(p.params[39], s.step_fraction * p.params[37]);
        } else if (p.params[37] > T(0) && s.scale.price_scale > T(0)) {
            if (u.price_scale != s.scale.price_scale) {
                s.step_fraction = std::min(T(1), s.step_fraction * p.params[38]);
            } else {
                State probe = s;  // pre-update committed state; no writes to s.
                auto px = x;
                if (price_target(probe, px, p, c) != s.scale.price_scale)
                    s.step_fraction = std::max(p.params[39], s.step_fraction * p.params[37]);
            }
        }
        if (s.scale.price_scale > T(0) && u.price_scale != s.scale.price_scale)
            s.last_scale_direction = u.price_scale > s.scale.price_scale ? 1 : -1;
        const T spot = MathOps<T>::get_p(u.xp, u.D, {c.A, T(0)}) * u.price_scale / c.precision;
        const auto block = x.block_timestamp / static_cast<uint64_t>(p.params[17]);
        if (!(spot > 0) || !(u.xcp_profit > 0))
            throw std::runtime_error("invalid fee signal observation");
        if (!s.initialized) {
            s.launch_ts = x.block_timestamp;
            s.variance = p.params[18] * p.params[18];
            s.absolute_return = p.params[18] * T(.7978845608028654);
            s.start_price = s.end_price = spot;
            s.start_profit = s.end_profit = u.xcp_profit;
            s.block = block;
            s.initialized = true;
        } else if (block != s.block) {
            const auto m = moments(s, p, x.block_timestamp);
            s.variance = m.variance;
            s.absolute_return = m.absolute_return;
            s.daily_profit = m.daily_profit;
            s.start_price = s.end_price;
            s.start_profit = s.end_profit;
            s.block = block;
        }
        s.end_price = spot;
        s.end_profit = u.xcp_profit;
        const auto clock_params = price_params(s, p);
        if (p.params[19] == T(0)) {
            PriceScale::update_state(s.scale, x, clock_params, c, u);
            Fee::update_state(s.fee, x, p, c, u);
        } else {
            Fee::update_state(s.fee, x, p, c, u);
            const auto& report = s.fee.base.report;
            const double age = double(x.block_timestamp) - report.timestamp;
            bool use_report = s.scale.price_scale > T(0) && report.price > T(0) &&
                              std::isfinite(age) && age >= 0 && age <= 300;
            if (use_report && p.params[19] == T(2))
                use_report = std::abs(std::log(report.price / s.scale.price_scale)) > p.params[41];
            const T observation = use_report ? report.price : u.last_prices;
            const PolicyUpdate<T> driven{u.xp, u.price_scale, u.price_oracle, observation,
                                         u.virtual_price, u.xcp_profit, u.D, u.oracle_timestamp};
            PriceScale::update_state(s.scale, x, clock_params, c, driven);
        }
        if (p.params[11] == T(7) || p.params[11] == T(8) || p.params[22] > T(0)) {
            const auto& report = s.fee.base.report;
            if (report.price > 0 && (!(s.report_price > 0) || report.timestamp > s.report_ts)) {
                if (s.report_price > 0) {
                    const T dt = T(report.timestamp - s.report_ts);
                    const T lambda = std::exp(-T(.693147180559945309L) * dt / p.params[12]);
                    const T change = std::log(report.price / s.report_price);
                    s.report_variance = lambda * s.report_variance +
                        (1 - lambda) * change * change * p.params[17] / dt;
                } else {
                    s.report_variance = p.params[18] * p.params[18];
                }
                s.report_price = report.price;
                s.report_ts = report.timestamp;
            }
        }
    }
};
} // namespace arb::pools::twocrypto_fx
