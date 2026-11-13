// Focused test for policies/report_lean.hpp: the pure target call and its half-gap step cap, the trend gate, report
// admission and ageing, the context fee floor, the driver's report lifetime (reportless touches, dust after an idle hour,
// a jump right after a delivery), the reportless observation (the spot stored by the previous touch, never the touch's
// own), and a pool that follows a trending report.
// Units: prices in coin 0 per coin 1, fees and caps as fractions, times in seconds.
#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#define TWOCRYPTO_POLICY_HEADER "pools/twocrypto_fx/policies/report_lean.hpp"
#include "pools/twocrypto_fx/helpers.hpp"
namespace fx = arb::pools::twocrypto_fx;
using P = fx::ChallengeFeePolicy<double>;
using Pool = fx::TwoCryptoPool<double>;
void require(bool value, const char* what) { if (!value) throw std::runtime_error(what); }
bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

int main() {
    static_assert(P::PARAM_COUNT == 9, "nine parameters");
    static_assert(std::is_invocable_v<decltype(&P::get_price_scale), const P::State&, fx::PolicyResearchContext<double>&,
                                      const fx::PolicyConfig<double>&, const fx::PolicyPoolConfig<double>&>,
                  "get_price_scale takes the policy state const");
    require(std::string_view(P::NAME) == "report_lean", "descriptor name");
    fx::PolicyConfig<double> p{fx::PolicyKind::Compiled, {}, P::PARAM_COUNT};
    for (std::size_t i = 0; i < P::PARAM_COUNT; ++i) p.params[i] = double(P::DESCRIPTOR.parameters[i].default_value);
    fx::PolicyPoolConfig<double> c{};
    c.A = 50000, c.precision = 1, c.fee_precision = 1;
    P::validate_params(p, c);

    const uint64_t t0 = 1'760'000'000;
    P::State s{};
    s.price_scale = s.fast_ema = s.slow_ema = s.last_prices = 2000;
    s.last_update_ts = t0;
    s.report = {2000, double(t0)};
    s.xp = {1e6, 1e6}, s.D = 2e6;

    {   // Pure target, step cap x 5: report 2100 after 600 s pulls the fast EMA (1 h) about 11 above the scale, far
        // beyond one capped step of 12 bp x 2000 = 2.4, so the pool is asked for 2000 + 5 x 2.4.
        fx::PolicyResearchContext<double> x{};
        x.block_timestamp = t0 + 600, x.price_feed = 2100, x.price_feed_timestamp = double(t0 + 600);
        x.report_max_age_s = 1e300;
        P::State seen;
        std::memcpy(&seen, &s, sizeof seen);
        const double asked = P::get_price_scale(s, x, p, c);
        // Semantic bytes only: the alpha memo after them is a non-semantic cache.
        require(!std::memcmp(&seen, &s, offsetof(P::State, alpha_cache)), "get_price_scale writes no semantic byte of the state");
        require(near(asked, 2000 + 5 * 2000 * p.params[7], 1e-9), "one capped step, times the pool's 1/5");
        // A small report move stays inside the cap: exactly half of the gap to the trend-gated target, times 5, with the
        // EMAs and the gate recomputed independently of the policy.
        x.price_feed = 2001;
        const double a_fast = std::exp(-double(P::LN2) * 600.0 / p.params[4]);
        const double a_slow = std::exp(-double(P::LN2) * 600.0 / p.params[5]);
        const double fast = 2001 * (1 - a_fast) + 2000 * a_fast;
        const double slow = 2001 * (1 - a_slow) + 2000 * a_slow;
        const double w = std::min(1.0, std::abs(fast / slow - 1) / p.params[6]);
        const double target = fast + w * (2001 - fast);
        require(target > 2000 && 0.5 * (target - 2000) < 2000 * p.params[7], "small move: inside the cap");
        const double small = P::get_price_scale(s, x, p, c);
        require(near(small, 2000 + 5 * (0.5 * (target - 2000)), 1e-12), "an uncapped half-gap step to the gated target");
    }
    {   // Step law move = min(gap / 2, step_cap * scale), times 5, at dt 0 with the gate off (target = the fast EMA):
        // zero gap, both directions, and just below / at / above the saturation knee gap = 2 * step_cap * scale.
        fx::PolicyConfig<double> g = p;
        g.params[6] = 0;
        fx::PolicyResearchContext<double> x{};
        x.block_timestamp = t0, x.report_max_age_s = 1e300;  // dt 0: EMAs as stored; no report delivered
        const double scale = 2000, cap = scale * g.params[7];
        auto ask = [&](double gap) {
            P::State t = s;
            t.fast_ema = scale + gap;
            return P::get_price_scale(t, x, g, c);
        };
        require(ask(0) == scale, "zero gap: the scale itself");
        for (const double sign : {1.0, -1.0}) {
            const double below = sign * 1.9 * cap, knee = sign * 2.0 * cap, above = sign * 2.1 * cap;
            require(near(ask(below), scale + 5 * 0.5 * below, 1e-9), "just below the knee: half the gap");
            require(near(ask(knee), scale + sign * 5 * cap, 1e-9), "at the knee: half the gap is one cap");
            require(near(ask(above), scale + sign * 5 * cap, 1e-9), "above the knee: one capped step");
        }
    }
    {   // End to end: one funded swap through TwoCryptoPool, with the native actuator clamps and the LP gate not
        // binding, commits exactly 1/5 of the policy's 5 * move offset (cached_price_scale - scale_before = move).
        fx::PolicyConfig<double> q = p;
        q.params[0] = .002, q.params[2] = .015, q.params[7] = .001;
        Pool pool({1, 1}, 50000, 0, .015, .015, .01, 1e-10, .03, 865, 2000, .5, 0, fx::PolicyKind::Compiled, q);
        pool.set_block_timestamp(t0);
        pool.refresh_policy_context();
        pool.clear_policy_price_feed();
        pool.add_liquidity({3e7, 1.5e4}, 0);
        const uint64_t now = t0 + 600;
        pool.set_block_timestamp(now);
        pool.refresh_policy_context(2010, double(now));
        const double scale_before = pool.cached_price_scale;
        const P::State before = pool.policy.compiled_state;
        require(before.price_scale == scale_before, "policy state committed at the pool's scale");
        pool.exchange(0, 1, 6e5, 0);
        auto x = pool.policy.research;  // the context of this touch's target call
        const double asked = P::get_price_scale(before, x, pool.policy.params, pool.policy.config);
        const double move = (asked - scale_before) / P::TWEAK_PRICE_MULTIPLIER;
        const double committed = pool.cached_price_scale - scale_before;
        require(move != 0 && std::abs(move) <= scale_before * q.params[7] * (1 + 1e-12), "a nonzero move within one cap");
        require(near(committed, move, 1e-9), "the pool commits move = (5 * move) / 5");
        pool.clear_policy_price_feed();
    }
    {   // Trend gate: with the EMAs trend_gate apart the target is the report itself; with the gate off, the fast EMA.
        P::State t = s;
        t.fast_ema = 2016, t.slow_ema = 2000;  // 80 bp apart: w = 1 at trend_gate 80 bp
        t.price_scale = 2050;
        t.report = {2040, double(t0 - 30)};
        fx::PolicyResearchContext<double> x{};
        x.block_timestamp = t0, x.price_feed = 2048, x.price_feed_timestamp = double(t0 + 1);  // dt 0: EMAs as stored
        x.report_max_age_s = 1e300;
        fx::PolicyConfig<double> wide = p;
        wide.params[7] = .01;
        require(near(P::get_price_scale(t, x, wide, c), 2050 - 5 * 1.0, 1e-9), "w = 1: half of the 2.0 gap to the report 2048");
        wide.params[6] = 0;
        require(near(P::get_price_scale(t, x, wide, c), 2050 - 5 * 17.0, 1e-9), "gate off: half of the 34.0 gap to the fast EMA");
    }
    {   // Admission and ageing: 6 s ahead admitted, 7 s not, 13 s old not, not newer not; age relax/4 gives w = 1/2.
        fx::PolicyResearchContext<double> x{};
        x.block_timestamp = t0 + 100, x.report_max_age_s = 1e300, x.price_feed = 2010;
        x.price_feed_timestamp = double(t0 + 106);
        require(P::admissible(s.report, x), "6 s ahead is admissible");
        x.price_feed_timestamp = double(t0 + 107);
        require(!P::admissible(s.report, x), "7 s ahead is not");
        x.price_feed_timestamp = double(t0 + 87);
        require(!P::admissible(s.report, x), "13 s old is not");
        x.price_feed_timestamp = double(t0);
        require(!P::admissible(s.report, x), "not newer than the stored report");
        x.price_feed = 0;
        P::State a = s;
        a.report = {2000, double(t0 + 100) - p.params[3] / 4};
        const auto terms = P::report_terms(a, p, c, x, 0);
        require(terms.usable && near(terms.weight, .5, 1e-12), "age relax/4: weight 1/2");
        a.report.timestamp = double(t0 + 100) - p.params[3];
        require(!P::report_terms(a, p, c, x, 0).usable, "age relax: unusable, every swap pays the fallback");
        // An inadmissible report is a touch without one: the stored report and the EMA observation ignore it (lifetime
        // -1: a touch without a delivered report observes the stored spot, 2000); the update stores its own spot.
        P::State u = s;
        fx::PolicyConfig<double> once = p;
        once.params[8] = -1;
        x.price_feed = 2500, x.price_feed_timestamp = double(t0 - 5);
        const std::array<double, 2> xp{1e6, 1e6};
        const double ps = 2000, last = 2003, one = 1, D = 2e6;
        P::update_state(u, x, once, c, {xp, ps, ps, last, one, one, D, x.block_timestamp});
        require(u.report.price == 2000 && u.fast_ema == 2000 && u.last_prices == last, "inadmissible report ignored");
    }
    {   // Context floor: a stored report at or behind the committed spot (2000 vs 2005) makes every coin-0-in swap pay F;
        // coin-1-in swaps toward it keep the aged base.
        P::State f = s;
        f.spot = 2005;
        fx::PolicyResearchContext<double> x{};
        x.block_timestamp = t0, x.report_max_age_s = 1e300;
        require(P::context_fee_floor(f, p, c, x, 0) == p.params[2], "no corrective coin-0-in swap: F");
        require(P::context_fee_floor(f, p, c, x, 1) == p.params[0], "fresh report, coin 1 in: the base");
    }
    {   // Reportless touch: a report older than report_lifetime_s (20 min vs 5 min) is not the observation; the spot
        // stored by the last update (2022) is, clamped to [scale / 2, 2 scale] for the target as for the EMAs.
        P::State rless = s;
        rless.fast_ema = 2016, rless.slow_ema = 2000;  // 80 bp apart: w = 1 at trend_gate 80 bp
        rless.price_scale = 2020, rless.last_prices = 2022;
        rless.report = {1900, double(t0 - 1200)};      // stale 20-minute-old report
        fx::PolicyResearchContext<double> x{};
        x.block_timestamp = t0, x.report_max_age_s = 1e300;  // dt 0: EMAs as stored
        fx::PolicyConfig<double> wide = p;
        wide.params[7] = .01, wide.params[8] = 300;
        require(near(P::get_price_scale(rless, x, wide, c), 2020 + 5 * 1.0, 1e-9), "reportless touch moves half of the 2.0 gap toward the stored spot 2022");
        rless.fast_ema = 2000.2, rless.last_prices = 5000;  // w = 1/80; the spot clamps to 2 x 2020
        const double gated = 2000.2 + (4040 - 2000.2) / 80;
        require(near(P::get_price_scale(rless, x, wide, c), 2020 + 5 * 0.5 * (gated - 2020), 1e-9), "the target uses the clamped spot");
    }
    {   // Pool smoke test: a report trending 2000 -> 2100 over 5 h drags the scale along under alternating swaps.
        fx::PolicyConfig<double> q = p;
        q.params[0] = .002, q.params[2] = .015, q.params[7] = .001;
        Pool pool({1, 1}, 50000, 0, .015, .015, .01, 1e-10, .03, 865, 2000, .5, 0, fx::PolicyKind::Compiled, q);
        uint64_t now = t0;
        pool.set_block_timestamp(now);
        pool.refresh_policy_context();
        pool.clear_policy_price_feed();
        pool.add_liquidity({3e7, 1.5e4}, 0);
        for (int k = 1; k <= 1500; ++k) {
            now += 12;
            const double report = 2000 + 100.0 * std::min(1.0, k / 1500.0);
            pool.set_block_timestamp(now);
            pool.refresh_policy_context(report, double(now));
            if (k % 2 == 0) pool.exchange(0, 1, 6e5, 0); else pool.exchange(1, 0, 300, 0);
            pool.clear_policy_price_feed();
        }
        require(pool.cached_price_scale > 2050 && pool.cached_price_scale < 2110, "the scale follows the trending report");
    }
    {   // Report lifetime L: a touch without its own report observes the stored report (2100) while its age < L, else
        // the stored spot (2000); L = 0 never observes a report, not even one delivered with the touch; L < 0 observes
        // the delivered one alone. The target call and the update integrate it; the update stores its own spot.
        const auto observe = [&](double lifetime, uint64_t age, double delivered) {
            fx::PolicyConfig<double> q = p;
            q.params[6] = 0, q.params[8] = lifetime;
            P::State t = s;
            t.report = {2100, double(t0)};
            fx::PolicyResearchContext<double> x{};
            x.block_timestamp = t0 + age, x.report_max_age_s = 1e300;
            if (delivered > 0) x.price_feed = delivered, x.price_feed_timestamp = double(x.block_timestamp);
            const double obs = P::observation(t, P::driver_report(t, q, x));
            const double asked = P::get_price_scale(t, x, q, c);
            P::update_state(t, x, q, c, {t.xp, 2000, 2000, 1990, 1, 1, t.D, x.block_timestamp});
            require((t.fast_ema > 2000) == (obs > 2000) && (asked > 2000 + 1e-6) == (obs > 2000),
                    "the target call and the update integrate the observation");
            require(t.last_prices == 1990, "the update stores its own spot");
            return obs;
        };
        for (const double L : {12.0, 300.0, 5400.0}) {
            require(observe(L, uint64_t(L) - 1, 0) == 2100, "age L - 1: the stored report");
            require(observe(L, uint64_t(L), 0) == 2000, "age L: last_prices");
        }
        require(observe(0, 0, 0) == 2000 && observe(0, 1, 2050) == 2000, "L = 0: never a report");
        require(observe(-1, 1, 0) == 2000 && observe(-1, 1, 2050) == 2050, "L = -1: the delivered report alone");
        require(observe(5400, 1, 2050) == 2050, "a delivered report is the observation and the new stored one");
    }
    {   // Research driver ratio: the driver observes a report times the ratio it arrived with (2 x 1050 = 2100), also
        // once stored and the context's ratio is back to 1; the fee terms keep the report itself.
        P::State t = s;
        fx::PolicyResearchContext<double> x{};
        x.block_timestamp = t0 + 12, x.report_max_age_s = 1e300, x.driver_ratio = 2;
        x.price_feed = 1050, x.price_feed_timestamp = double(t0 + 12);
        require(P::driver_report(t, p, x) == 2100 && P::report_terms(t, p, c, x, 0).price == 1050, "delivered: driver x 2, fee x 1");
        P::update_state(t, x, p, c, {t.xp, 2000, 2000, 2000, 1, 1, t.D, x.block_timestamp});
        x.price_feed = 0, x.driver_ratio = 1, x.block_timestamp = t0 + 24;
        require(P::driver_report(t, p, x) == 2100 && P::report_terms(t, p, c, x, 0).price == 1050, "stored: the ratio of its admission");
        x.driver_scale = 0.5;  // the touch's factor applies to the stored report too; the fee terms keep the report
        require(P::driver_report(t, p, x) == 1050 && P::report_terms(t, p, c, x, 0).price == 1050, "driver_scale of the touch");
    }
    {   // Through TwoCryptoPool: a report at the pool's price (2000, so the delivering swap steps nothing) arrives with a
        // swap, then one reportless touch. Dust either way after an idle hour observes the stored report while 3600 s < L,
        // else the spot the delivering swap left (`before`); a touch 13 s after the delivery observes the report for
        // L = 300 and `before` for L = 12, also when its own swap moves the spot (`after`). The observation is read back
        // from the fast EMA, which starts the touch at 2000: fast = 2000 + (obs - 2000) (1 - alpha).
        struct Seen { double observed, before, after; };
        const auto touch = [&](double lifetime, uint64_t idle, int coin_in, double dx) {
            fx::PolicyConfig<double> q = p;
            q.params[0] = .002, q.params[2] = .015, q.params[7] = .001, q.params[8] = lifetime;
            Pool pool({1, 1}, 50000, 0, .015, .015, .01, 1e-10, .03, 865, 2000, .5, 0, fx::PolicyKind::Compiled, q);
            pool.set_block_timestamp(t0);
            pool.refresh_policy_context();
            pool.clear_policy_price_feed();
            pool.add_liquidity({3e7, 1.5e4}, 0);
            pool.set_block_timestamp(t0 + 12);
            pool.refresh_policy_context(2000, double(t0 + 12));
            pool.exchange(0, 1, 6e5, 0);
            pool.clear_policy_price_feed();
            const double before = pool.last_prices;
            pool.set_block_timestamp(t0 + 12 + idle);
            pool.refresh_policy_context();
            pool.exchange(coin_in, 1 - coin_in, dx, 0);
            const double weight = 1 - std::exp(-double(P::LN2) * double(idle) / q.params[4]);
            return Seen{2000 + (pool.policy.compiled_state.fast_ema - 2000) / weight, before, pool.last_prices};
        };
        for (const int coin_in : {0, 1}) {
            const double dust = coin_in == 0 ? 1e-6 : 5e-10;
            require(near(touch(5400, 3600, coin_in, dust).observed, 2000, 1e-9), "idle hour, L = 90 min: dust observes the stored report");
            const Seen raw = touch(1800, 3600, coin_in, dust);
            require(near(raw.observed, raw.before, 1e-9) && raw.before > 2001, "idle hour, L = 30 min: dust observes the stored spot");
        }
        require(near(touch(300, 13, 0, 6e5).observed, 2000, 1e-6), "13 s after the delivery, L = 300: the report");
        const Seen late = touch(12, 13, 0, 6e5);
        require(near(late.observed, late.before, 1e-6) && late.after > late.before + 1,
                "13 s after the delivery, L = 12: the stored spot, not the touch's own");
    }
    {   // Jump right after a delivery: scale, EMAs, spot and the report at 2000, then the market jumps 10% and only
        // reportless touches follow, each leaving the spot at 2161.5 (2200 less a 175 bp fallback wedge), every 12 s or
        // once after 30 min. While the report is younger than L the EMAs stay at 2000 (the trend gate never sees the
        // jump). Past L each touch observes the spot the previous one left: 12 s callbacks follow the jump one touch
        // late, a single touch after 30 min sees only the spot before its own trade.
        const auto fast_after = [&](double lifetime, uint64_t step) {
            fx::PolicyConfig<double> q = p;
            q.params[8] = lifetime;
            P::State t = s;
            for (uint64_t now = t0 + step; now <= t0 + 1800; now += step) {
                fx::PolicyResearchContext<double> x{};
                x.block_timestamp = now, x.report_max_age_s = 1e300;
                P::update_state(t, x, q, c, {t.xp, 2000, 2000, 2161.5, 1, 1, t.D, now});
            }
            return t.fast_ema;
        };
        require(near(fast_after(5400, 12), 2000, 1e-9) && near(fast_after(5400, 1800), 2000, 1e-9),
                "L = 90 min: the jump is invisible to the EMAs");
        for (const double L : {0.0, 300.0})
            require(fast_after(L, 12) > 2001 && near(fast_after(L, 1800), 2000, 1e-9), "past L: the previous touch's spot");
        require(fast_after(0, 12) > fast_after(300, 12) + 1, "L = 0 follows the spot from the first touch");
    }
    return 0;
}
