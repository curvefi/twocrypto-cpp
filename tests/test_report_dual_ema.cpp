// Focused test for policies/report_dual_ema.hpp: fee surface, report admission and persistence, and the
// harness contract g (1 - fee) = max((1-F) g, min(D g, A g + B x)) iff g'(x) >= k, else (1-F) g.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <tuple>
#define TWOCRYPTO_POLICY_HEADER "pools/twocrypto_fx/policies/report_dual_ema.hpp"
#include "pools/twocrypto_fx/helpers.hpp"
namespace fx = arb::pools::twocrypto_fx;
using P = fx::ChallengeFeePolicy<double>;
using Pool = fx::TwoCryptoPool<double>;
using Arr = std::array<double, 2>;
using Params = std::array<double, 13>;
void require(bool value, const char* what) { if (!value) throw std::runtime_error(what); }

fx::PolicyConfig<double> config(const Params& v) {
    fx::PolicyConfig<double> p{fx::PolicyKind::Compiled, {}, v.size()};
    std::copy(v.begin(), v.end(), p.params.begin());
    return p;
}
// Unit-precision pool seeded at block `now`; context report age unbounded (harness default).
Pool make(const Params& v, double A, double scale, Arr balances, uint64_t now) {
    Pool pool({1, 1}, A, 0, .0005, .005, .01, 1e-10, .03, 600, scale, .5, .5, fx::PolicyKind::Compiled, config(v));
    pool.set_block_timestamp(now);
    pool.policy.research.report_max_age_s = 1e300;
    pool.add_liquidity(balances, 0);
    return pool;
}
void at(Pool& pool, uint64_t now, double price = 0, double ts = 0) {  // deliver (price, ts) if price > 0, else keep
    pool.set_block_timestamp(now);
    pool.refresh_policy_context();
    pool.clear_policy_price_feed();
    if (price > 0) pool.policy.set_price_feed(price, ts);
}
auto terms(const Pool& pool, std::size_t i) { return *pool.policy.report_terms(pool.policy.research, i); }
double spot(const Pool& pool, const Arr& xp) {  // coin0 per coin1
    return fx::MathOps<double>::get_p(xp, pool.D, {pool.A, pool.gamma}) * pool.cached_price_scale;
}
std::pair<Arr, double> swap(const Pool& pool, std::size_t i, double x) {  // pre-fee xp, gross output tokens
    const auto [xp, dy] = fx::post_swap_xp(pool, i, 1 - i, x, pool.cached_price_scale);
    return {xp, fx::xp_to_tokens_j(pool, 1 - i, dy, pool.cached_price_scale)};
}

// Hinge fresh fee, signed ages with delta, continuity at T, guards; admission and persistence table.
void fee_surface() {
    const Params v{.003, .8, .5, .015, 1642, 12961, 1.31, 0, .0005, .005, 2, 2, 300};
    const double b = v[0], c = v[1], F = v[3];
    constexpr uint64_t B0 = 1'760'000'003, B = B0 + 12;
    const Pool base = make(v, 50000, 2000, {1.02e6, 490}, B0);  // spot above scale: coin 0 in is away
    const Arr old = fx::pool_xp_current(base);
    const double P0 = spot(base, old) * 1.01, P1 = P0 * 1.002;
    const auto seeded = [&](double ts) { Pool q = base; at(q, B0, P0, ts); q.tick(); return q; };
    Pool s = seeded(B0 + 2);  // stored report 2 s after its block
    const Arr xp = swap(s, 0, 1000).first;
    const double e = 1 - (xp[0] - old[0]) / ((old[1] - xp[1]) * P0 / 2000), w0 = std::sqrt(2 / 302.);
    const double fresh = std::min(F, b + c * std::max(0.0, e - b));  // v6's max(b, c e) is lower by b (1 - c)
    const auto fee = [&](uint64_t now) { at(s, now); return s.fee(xp); };
    require(fresh < F && fresh - std::max(b, c * e) > 1e-4 && std::abs(fee(B0) - fresh) < 1e-15,
            "age -2 with delta 2 pays the hinge fresh fee");
    require(std::abs(fee(B0 + 2) - ((1 - w0) * fresh + w0 * F)) < 1e-15, "age 0 weight sqrt(delta / (T + delta))");
    require(fee(B0 + 301) < F && F - fee(B0 + 301) <= (F - b) / 302 && fee(B0 + 302) == F && !terms(s, 0).usable,
            "ages 299 and 300: continuous and expired at T");
    Params brief = v; brief[12] = 60;  // aging_s: the same stored report expires at age 60 instead of 300
    Pool r = make(brief, 50000, 2000, {1.02e6, 490}, B0);
    at(r, B0, P0, B0 + 2.); r.tick(); at(r, B0 + 61);
    const bool live = terms(r, 0).usable && r.fee(xp) < F;
    at(r, B0 + 62);
    require(live && !terms(r, 0).usable && r.fee(xp) == F, "aging_s sets the stored report's horizon");
    at(s, B0 + 2);
    const Arr add = fx::pool_xp_from(s, {s.balances[0] + 1000, s.balances[1] + .5}, 2000.);
    const auto over = swap(s, 0, 5e4);
    require(s.policy.get_liquidity_fee(add, s.D) == F && s.fee(swap(s, 1, .5).first) == F && s.fee(xp) < F &&
            spot(s, over.first) > P0 && 5e4 < over.second * P0 && s.fee(over.first) == F &&
            s.policy.get_fee(xp, s.D + 1) == F, "LP add, wrong direction, overshoot and live D != D pay F");
    // Stored report at B0 - 2: a delivered report is priced iff -2 <= age <= 12 and newer, and the swap commit
    // persists it (the next block keeps it); otherwise the stored report stays.
    const std::tuple<uint64_t, double, bool> rows[] = {{B, B + 3., false}, {B, B + 2., true}, {B, B, true},
        {B, B - 12., true}, {B, B - 13., false}, {B0, B0 - 2., false}};  // ages -3 -2 0 12 13, equal timestamp
    for (const auto& [block, ts, admitted] : rows) {
        Pool q = seeded(B0 - 2.);
        at(q, block, P1, ts);
        const bool priced = terms(q, 0).price == (admitted ? P1 : P0);
        q.exchange(0, 1, 1000, 0);
        at(q, block + 12);
        require(priced && terms(q, 0).price == (admitted ? P1 : P0), "report priced and persisted iff admitted");
    }
}

// Random pools, reports (signed ages) and swaps: report_terms follow the spec and the pool's net output
// g (1 - fee) is max((1-F) g, min(D g, A g + B x)) iff g'(x) >= k and edge > 0, else (1-F) g.
void harness_contract() {
    std::mt19937_64 rng(2026);
    std::uniform_real_distribution<double> u(0, 1);
    const auto lu = [&](double lo, double hi) { return lo * std::pow(hi / lo, u(rng)); };
    constexpr int N = 24000;
    int eligible = 0, boundary = 0;
    double worst = 0;
    for (int n = 0; n < N; ++n) {
        const double F = lu(.001, .05), delta = 12 * u(rng), fw = 3 * u(rng), scale = lu(100, 1e4);
        const Params v{lu(1e-5, F), u(rng), u(rng), F, 1642, 12961, 1.31, 0, .0005, .005, delta, fw, 300};
        const uint64_t B0 = 1'760'000'003 + 12 * uint64_t(n);
        Pool pool = make(v, lu(2e4, 4e5), scale, {1e6, 1e6 / scale * lu(.5, 2)}, B0);
        const double max_age = pool.policy.research.report_max_age_s = u(rng) < .5 ? 1e300 : 12 * u(rng);
        const std::size_t i = rng() % 2;
        const Arr old = fx::pool_xp_current(pool);
        const double s0 = spot(pool, old), x = pool.balances[i] * lu(1e-3, .5);
        const auto [xp, g] = swap(pool, i, x);
        const double after = spot(pool, xp);
        const auto price = [&] { return s0 * std::exp((u(rng) < .5 ? -1 : 1) * lu(1e-5, .03)); };
        // Stored report offered at B0; every 8th case delivers within 2 ulps of the post-trade spot instead.
        const bool probe = n % 8 == 0;
        const double u0 = B0 - (17 * u(rng) - 4), P0 = price();
        if (!probe) { at(pool, B0, P0, u0); pool.tick(); }
        const uint64_t B = B0 + (u(rng) < .3 ? 0 : uint64_t(400 * u(rng)));
        double P1 = probe ? after : price(), u1 = probe ? B : B - (17 * u(rng) - 4);
        for (int m = probe ? int(rng() % 5) - 2 : 0; m != 0; m -= m > 0 ? 1 : -1) P1 = std::nextafter(P1, m * 1e300);
        const bool deliver = probe || u(rng) < .5;
        at(pool, B, deliver ? P1 : 0, u1);
        const bool stored = !probe && B0 - u0 >= -fw && B0 - u0 <= std::min(12., max_age);
        const bool fresh = deliver && B - u1 >= -fw && B - u1 <= std::min(12., max_age) && (!stored || u1 > u0);
        const double Pe = fresh ? P1 : stored ? P0 : 0, age = B - (fresh ? u1 : u0);
        const bool usable = Pe > 0 && age >= -fw && age < 300;
        const double w = usable ? std::sqrt(std::clamp((age + delta) / (300 + delta), 0., 1.)) : 1;
        const double ratio = fx::MathOps<double>::get_p(old, pool.D, {pool.A, 0});
        const double c = (i == 0 ? ratio < 1 : ratio > 1) ? v[1] * v[2] : v[1];
        const auto t = terms(pool, i);
        require(t.usable == usable && (!usable || (t.price == Pe && std::abs(t.weight - w) < 1e-15)) &&
                t.base == v[0] && t.capture == c && t.fallback == F, "report terms follow the spec");
        const double fee = pool.fee(xp), net = g * (1 - fee);
        require(pool.prepare_fee()(xp) == fee && pool.context_fee_lower_bound(i) <= fee, "prepared fee and floor");
        const double k = i == 0 ? 1 / Pe : Pe, rate = i == 0 ? 1 / after : after;
        const double Dm = (1 - w) * (1 - v[0]) + w * (1 - F), Am = (1 - w) * (1 - c) * (1 - v[0]) + w * (1 - F);
        const double disc = std::max((1 - F) * g, std::min(Dm * g, Am * g + (1 - w) * c * k * x));
        const bool corrective = usable && rate >= k && 1 - k * x / g > 0;
        const double err = std::abs(net - disc) / disc;
        if (corrective ? !(err < 1e-12) : fee != F) {  // the guard and g'(x) >= k may differ only at the boundary
            require(usable && std::abs(rate - k) <= 1e-12 * k && (corrective ? fee == F : err < 1e-12),
                    "net output follows the harness contract");
            ++boundary;
        } else if (corrective) {
            ++eligible, worst = std::max(worst, err);
        }
        // A size that profits against `ext` (bid for coin 0 in, ask for coin 1 in) must pass the profit gate.
        const double ext = (i == 0 ? x / net : net / x) * (1 + 2e-3 * (u(rng) - .5));
        require((i == 0 ? net * ext <= x : net <= x * ext) || pool.context_may_profit(i, s0, ext), "profit gate");
    }
    std::printf("harness contract: %d cases, %d eligible, max rel err %.2e, %d boundary-tolerance\n",
                N, eligible, worst, boundary);
    require(eligible > N / 10 && boundary < N / 100, "harness contract coverage");
}

// Descriptor defaults validate, timing parameters are bounded, feeds may lead the block by 60 s.
void bounds() {
    Params v{};
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = double(P::DESCRIPTOR.parameters[i].default_value);
    fx::PolicyPoolConfig<double> c; c.A = 50000;
    P::validate_params(config(v), c);
    const auto throws = [](auto f) { try { f(); } catch (const std::invalid_argument&) { return true; } return false; };
    for (const auto& [k, bad] : {std::pair{10, -.5}, std::pair{10, 12.5}, std::pair{11, -.5}, std::pair{11, 3.5},
                                 std::pair{12, .5}, std::pair{12, 3600.5}}) {
        Params q = v;
        q[k] = bad;
        require(throws([&] { P::validate_params(config(q), c); }), "timing parameter bounds");
    }
    fx::PolicyModel<double> m(config(v));
    m.set_block_timestamp(1000);
    m.set_price_feed(2000, 1060);
    require(throws([&] { m.set_price_feed(2000, 1060.5); }), "feed at most 60 s after the block");
}

int main() { bounds(); fee_surface(); harness_contract(); return 0; }
