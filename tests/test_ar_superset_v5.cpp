// Focused test for autoresearch/ar-superset-v5.hpp: report-latency grace (p43).
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>
#define TWOCRYPTO_POLICY_HEADER "pools/twocrypto_fx/policies/autoresearch/ar-superset-v5.hpp"
#include "pools/twocrypto_fx/policy.hpp"
namespace fx = arb::pools::twocrypto_fx;
using P = fx::ChallengeFeePolicy<double>;
using Arr = std::array<double, 2>;
void require(bool value, const char* what) { if (!value) throw std::runtime_error(what); }

// b60 c.75 F200bp sqrt 300s admission 12s, native driver, shape 0; p43 grace.
fx::PolicyConfig<double> params(double grace) {
    fx::PolicyConfig<double> p; p.kind = fx::PolicyKind::Compiled; p.n_params = 44;
    const double v[44] = {.006, .75, .02, 300, 12, 1200, 14400, 1.2, 0, .0005, .006, 0, 7200, 0, 0, .025, .025, 12,
                          .00032, 0, 0, 0, 0, .00032, .25, 4, 0, 0, 0, 1, .01, 0, .06, 600, 0, 0, -.4, 0, 2, .0625, 1, .01, 0, grace};
    for (std::size_t i = 0; i < 44; ++i) p.params[i] = v[i];
    return p;
}

void grace_ages_reports_without_changing_selection() {
    fx::PolicyPoolConfig<double> c; c.A = 50000; c.precision = c.fee_precision = 1; c.adjustment_step_min = 1e-10; c.adjustment_step_max = .03;
    const auto g0 = params(0), g2 = params(2);
    const Arr pre{1.01e6, .99e6};
    P::State s{};
    for (uint64_t k : {1'000'000ULL, 1'000'060ULL}) {   // cached report committed at 1'000'059 (price 2004)
        fx::PolicyResearchContext<double> x{}; x.block_timestamp = k; x.report_max_age_s = 12; x.price_feed = 2004; x.price_feed_timestamp = double(k) - 1;
        const fx::PolicyUpdate<double> up{pre, 2000, 2000, 2000, 1.01, 1.02, 2e6, k};
        P::update_state(s, x, g0, c, up);
    }
    std::mt19937_64 rng(11); std::uniform_real_distribution<double> u(0, 1);
    for (int n = 0; n < 300; ++n) {
        const uint64_t now = 1'000'060 + 1 + uint64_t(20 * u(rng));
        const double age = 16 * u(rng);                // offered report age (admissible only <= 12)
        fx::PolicyResearchContext<double> x{}; x.block_timestamp = now; x.report_max_age_s = 12;
        x.price_feed = 2000 * (1 + .01 * (u(rng) - .2)); x.price_feed_timestamp = double(now) - age;
        const double d = 200 + 3000 * u(rng);
        const Arr xp = u(rng) < .5 ? Arr{pre[0] + d, pre[1] - .999 * d} : Arr{pre[0] - .999 * d, pre[1] + d};
        const double f0 = P::get_fee(s, g0, c, x, xp, 2e6), f2 = P::get_fee(s, g2, c, x, xp, 2e6);
        require(P::prepare_fee(s, g2, c, x, 2e6)(xp) == f2 && P::prepare_fee(s, g0, c, x, 2e6)(xp) == f0, "prepared equals direct");
        require(f2 <= f0 + 1e-15, "grace never raises a fee");
        // A report at most 2s old quotes like a fresh one: equal to grace-0 quote at age ~0.
        if (age <= 2) {
            auto xf = x; xf.price_feed_timestamp = double(now);
            require(std::abs(f2 - P::get_fee(s, g0, c, xf, xp, 2e6)) < 1e-4, "young report treated as fresh");  // tie-break adds 1e-4 s per s of age
        }
        for (std::size_t i = 0; i < 2; ++i)   // bounds stay below the charged fee
            require(P::context_fee_floor(s, g2, c, x, i) <= f2 + 1e-12, "fee floor below graced fee");
    }
    // Admission unchanged: a 12.5s-old offered report is still rejected (cached 2004 report used).
    fx::PolicyResearchContext<double> late{}; late.block_timestamp = 1'000'070; late.report_max_age_s = 12;
    late.price_feed = 2100; late.price_feed_timestamp = 1'000'057.5;
    auto cached = late; cached.price_feed = 0;
    const Arr buy1{pre[0] + 1000, pre[1] - 999};
    require(P::get_fee(s, g2, c, late, buy1, 2e6) == P::get_fee(s, g2, c, cached, buy1, 2e6), "admission window unchanged");
    // Unix-scale timestamps: a 0s-old offered report must still beat a 1s-old cached one inside the grace.
    P::State us{};
    for (uint64_t k : {1'760'000'000ULL, 1'760'000'060ULL}) {
        fx::PolicyResearchContext<double> x{}; x.block_timestamp = k; x.report_max_age_s = 12; x.price_feed = 2004; x.price_feed_timestamp = double(k) - 1;
        const fx::PolicyUpdate<double> up{pre, 2000, 2000, 2000, 1.01, 1.02, 2e6, k};
        P::update_state(us, x, g0, c, up);
    }
    fx::PolicyResearchContext<double> fresh{}; fresh.block_timestamp = 1'760'000'060; fresh.report_max_age_s = 12;
    fresh.price_feed = 2030; fresh.price_feed_timestamp = 1'760'000'060;
    const double with_new = P::get_fee(us, g2, c, fresh, buy1, 2e6);
    auto none = fresh; none.price_feed = 0;
    require(with_new != P::get_fee(us, g2, c, none, buy1, 2e6), "offered fresh report selected at unix timestamps");
}

int main() { grace_ages_reports_without_changing_selection(); return 0; }
