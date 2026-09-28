// Report-fee shapes extracted from the validated isolated report-driver policy.
// Shape0: original sqrt aging;1: residual-share aging;2: residual through12s,
// original afterward;3: monotone upper-envelope hybrid. An optional allowance
// decays with sqrt age and is available only in shape3's young-report branch.
// No timestamp/provenance rewriting. Floating research only.
#pragma once
#include "oraclized_fair_fee.hpp"
namespace arb::pools::twocrypto_fx {
template <typename T, std::size_t ShapeIndex,
          std::size_t AllowanceIndex = std::numeric_limits<std::size_t>::max()>
struct ShapedOraclizedFairFeePolicy : OraclizedFairFeePolicy<T> {
    static_assert(std::is_floating_point_v<T>, "shaped report fee requires floating arithmetic");
    using Fee = OraclizedFairFeePolicy<T>;
    using State = typename Fee::State;
    using Report = typename Fee::Report;
    static constexpr double HYBRID_RESIDUAL_MAX_AGE_S = 12.0;
    // Modes2/3 use residual pricing for young reports. For older reports,
    // mode3 takes the maximum of the two shapes, preserving monotone aging.
    static bool residual(const PolicyConfig<T>& p, double age) {
        if (p.params[ShapeIndex] == T(1)) return true;
        return (p.params[ShapeIndex] == T(2) || p.params[ShapeIndex] == T(3)) &&
               std::isfinite(age) && age >= 0 && age <= HYBRID_RESIDUAL_MAX_AGE_S;
    }

    static T allowance(const PolicyConfig<T>& p, double age) {
        if constexpr (AllowanceIndex == std::numeric_limits<std::size_t>::max()) return T(0);
        else {
            if (p.params[ShapeIndex] != T(3) || !std::isfinite(age) || age < 0 ||
                age > HYBRID_RESIDUAL_MAX_AGE_S) return T(0);
            const T weight = T(std::sqrt(std::min(age / double(p.params[3]), 1.0)));
            return p.params[AllowanceIndex] * (T(1) - weight);
        }
    }

    static bool expired(const Report& report, double age, const PolicyConfig<T>& p) {
        return !(report.price > T(0)) || !std::isfinite(age) || age < 0 ||
               age >= static_cast<double>(p.params[3]);
    }

    // Parameters whose capture slot holds c_age * precision. Exactly the input
    // capture when w == 0, so age-zero quotes match mode 0 bit for bit.
    static PolicyConfig<T> aged_params(const PolicyConfig<T>& p, const PolicyPoolConfig<T>& c, double age) {
        PolicyConfig<T> out = p;
        if (!std::isfinite(age) || !(age > 0)) return out;
        const double horizon = static_cast<double>(p.params[3]);
        const T w = T(std::sqrt(std::min(age, horizon) / horizon));
        const T capture = p.params[1] / c.precision;
        out.params[1] = (T(1) - (T(1) - capture) * (T(1) - w)) * c.precision;
        return out;
    }

    // `aged` differs from the input params only in the capture slot; `base` and
    // `full` are the input base fee and fallback.
    static T residual_fee(const State& s, const PolicyConfig<T>& aged, T base, T full,
                          const PolicyPoolConfig<T>& c, const Report& report,
                          const std::array<T, 2>& xp, const T& live_D, double age, T hedge_allowance) {
        std::size_t coin_in = 0;
        T fee;
        if (!(hedge_allowance > T(0))) {
            fee = Fee::quoted_fee(s, aged, c, report, xp, live_D, age, coin_in);
        } else {
            // Reuse every native quote guard. With zero capture a valid
            // discounted swap returns base; all invalid cases return fallback.
            auto guards = aged;
            guards.params[1] = T(0);
            if (Fee::quoted_fee(s, guards, c, report, xp, live_D, age, coin_in) >= full)
                return full;
            const T edge = Fee::reported_edge(s, report, c, xp, coin_in);
            const T excess = std::max(T(0), T(edge - base - hedge_allowance));
            fee = std::min(full, T(base + aged.params[1] * excess / c.precision));
        }
        if (fee >= full || !Fee::corrective(s, c, report, xp, coin_in)) return full;
        return std::max(fee, base);
    }

    // ---- Fee side: residual-header formulas; shape chosen by residual(p, age) --
    static T get_fee(const State& s, const PolicyConfig<T>& p, const PolicyPoolConfig<T>& c,
                     const PolicyResearchContext<T>& x, const std::array<T, 2>& xp, const T& live_D) {
        const Report report = Fee::effective_report(s, p, x);
        const double age = static_cast<double>(x.block_timestamp) - report.timestamp;
        const bool use_residual = residual(p, age);
        const bool envelope = p.params[ShapeIndex] == T(3) && !use_residual;
        if (!use_residual && !envelope) return Fee::get_fee(s, p, c, x, xp, live_D);
        const T fee = residual_fee(s, aged_params(p, c, age), p.params[0], Fee::fallback(p, c),
                                  c, report, xp, live_D, age, allowance(p, age));
        return envelope ? std::max(fee, Fee::get_fee(s, p, c, x, xp, live_D)) : fee;
    }

    // Ephemeral quote: valid only while state, parameters and context are frozen.
    static auto prepare_fee(const State& s, const PolicyConfig<T>& p, const PolicyPoolConfig<T>& c,
                            const PolicyResearchContext<T>& x, const T& live_D) {
        auto original = Fee::prepare_fee(s, p, c, x, live_D);
        const Report report = Fee::effective_report(s, p, x);
        const double age = static_cast<double>(x.block_timestamp) - report.timestamp;
        const bool use_residual = residual(p, age);
        const bool envelope = p.params[ShapeIndex] == T(3) && !use_residual;
        const PolicyConfig<T> aged = use_residual || envelope ? aged_params(p, c, age) : p;
        const T hedge_allowance = allowance(p, age);
        const T base = p.params[0];
        const T full = Fee::fallback(p, c);
        const State& state = s;
        const PolicyPoolConfig<T>& config = c;
        return [original, use_residual, envelope, &state, &config, aged, base, full, report, age, live_D, hedge_allowance](
                   const std::array<T, 2>& xp) -> T {
            if (!use_residual && !envelope) return original(xp);
            const T fee = residual_fee(state, aged, base, full, config, report, xp, live_D, age, hedge_allowance);
            return envelope ? std::max(fee, original(xp)) : fee;
        };
    }

    static T fee_floor(const PolicyConfig<T>& p, const PolicyPoolConfig<T>& c, const T& native) {
        return Fee::fee_floor(p, c, native);
    }

    // Residual shape: every fee is >= base, and every swap pays fallback when
    // the effective report is missing or expired. Original shape: its bound.
    static T context_fee_floor(const State& s, const PolicyConfig<T>& p, const PolicyPoolConfig<T>& c,
                               const PolicyResearchContext<T>& x, std::size_t i) {
        const Report report = Fee::effective_report(s, p, x);
        const double age = static_cast<double>(x.block_timestamp) - report.timestamp;
        if (!residual(p, age)) return Fee::context_fee_floor(s, p, c, x, i);
        return expired(report, age, p) ? Fee::fallback(p, c) : Fee::fee_floor(p, c, T(0));
    }

    // Execution worsens with size, so the report edge cannot exceed its spot
    // value. Profit requires g(e)=1-rho*(1-e)-fee(e)>0. The residual fee is
    // piecewise affine with kinks at base+allowance and the fallback crossing.
    // Ignoring the corrective check only lowers the fee. For older shape3
    // reports the original bound is conservative: actual fee is at least it.
    static bool context_may_profit(const State& s, const PolicyConfig<T>& p, const PolicyPoolConfig<T>& c,
                                   const PolicyResearchContext<T>& x, std::size_t i, T spot, T external) {
        const Report report = Fee::effective_report(s, p, x);
        const double age = static_cast<double>(x.block_timestamp) - report.timestamp;
        if (!residual(p, age)) return Fee::context_may_profit(s, p, c, x, i, spot, external);
        if (!(spot > T(0)) || !(external > T(0)) ||
            !std::isfinite(spot) || !std::isfinite(external)) return true;
        const T full = Fee::fallback(p, c), slack = T(1e-9);
        if (expired(report, age, p))
            return i == 0 ? (T(1) - full) * external > spot * (T(1) - slack)
                          : (T(1) - full) * spot > external * (T(1) - slack);
        const T rho = i == 0 ? report.price / external : external / report.price;
        const T edge_max = i == 0 ? T(1) - spot / report.price : T(1) - report.price / spot;
        const T base = p.params[0];
        const T kink = base + allowance(p, age);
        const T capture = aged_params(p, c, age).params[1] / c.precision;
        const auto margin = [&](T edge) {
            const T fee = std::min(full, T(base + capture * std::max(T(0), edge - kink)));
            return T(1) - rho * (T(1) - edge) - fee;
        };
        T best = std::max(margin(edge_max), margin(std::min(edge_max, kink)));
        if (capture > T(0))
            best = std::max(best, margin(std::min(edge_max, kink + (full - base) / capture)));
        return best > -slack;
    }

};
} // namespace arb::pools::twocrypto_fx
