#include <array>
#include <stdexcept>

#include "pools/twocrypto_fx/twocrypto.hpp"

namespace fx = arb::pools::twocrypto_fx;
using Pool = fx::TwoCryptoPool<double>;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

fx::PolicyConfig<double> policy_config(fx::PolicyKind kind) {
    fx::PolicyConfig<double> config{};
    config.kind = kind;
#ifdef TWOCRYPTO_POLICY_HEADER
    if (kind == fx::PolicyKind::Compiled) {
        config.params = {7500.0, 54000.0, 1.27, 0.00028, 0.0012, 0.0048};
        config.n_params = 6;
    }
#endif
    return config;
}

Pool make_pool(fx::PolicyKind kind) {
    return Pool(
        {1.0, 1.0}, 50'000.0, 1e-4, 1e-3, 2e-3, 1e-2, 1e-10, 5e-3,
        865.0, 1.0, 0.5, 0.5, kind, policy_config(kind)
    );
}

bool same_pool_state(const Pool& a, const Pool& b) {
    return a.balances == b.balances &&
        a.D == b.D &&
        a.totalSupply == b.totalSupply &&
        a.cached_price_scale == b.cached_price_scale &&
        a.cached_price_oracle == b.cached_price_oracle &&
        a.last_prices == b.last_prices &&
        a.last_timestamp == b.last_timestamp &&
        a.block_timestamp == b.block_timestamp &&
        a.donation_shares == b.donation_shares &&
        a.last_donation_release_ts == b.last_donation_release_ts &&
        a.policy.research.block_timestamp == b.policy.research.block_timestamp &&
        a.policy.research.price_oracle == b.policy.research.price_oracle &&
        a.policy.research.price_feed == b.policy.research.price_feed &&
        a.policy.research.price_feed_timestamp ==
            b.policy.research.price_feed_timestamp;
}

#ifdef TWOCRYPTO_POLICY_HEADER
template <typename State>
bool same_compiled_state(const State& a, const State& b) {
    return a.last_update_ts == b.last_update_ts &&
        a.last_prices == b.last_prices &&
        a.fast_ema == b.fast_ema &&
        a.slow_ema == b.slow_ema &&
        a.price_scale == b.price_scale &&
        a.alpha_cache.valid == b.alpha_cache.valid &&
        a.alpha_cache.dt == b.alpha_cache.dt &&
        a.alpha_cache.fast_half_life == b.alpha_cache.fast_half_life &&
        a.alpha_cache.slow_half_life == b.alpha_cache.slow_half_life &&
        a.alpha_cache.fast_alpha == b.alpha_cache.fast_alpha &&
        a.alpha_cache.slow_alpha == b.alpha_cache.slow_alpha;
}

void test_compiled_policy_rollback() {
    Pool pool = make_pool(fx::PolicyKind::Compiled);
    pool.set_block_timestamp(1'700'000'000);
    pool.add_liquidity({1'000'000.0, 1'000'000.0}, 0.0);
    pool.advance_time(97);
    pool.exchange(0.0, 1.0, 1'000.0, 0.0);
    const Pool expected = pool;
    const auto expected_policy = pool.policy.compiled_state;
    const auto snapshot = pool.mutable_snapshot();

    pool.balances = {101.0, 202.0};
    pool.D = 303.0;
    pool.totalSupply = 404.0;
    pool.policy.research = {2'020, 2'121.0, 2'222.0, 2'323};
    auto& state = pool.policy.compiled_state;
    state.last_update_ts = 3'030;
    state.last_prices = 3'131.0;
    state.fast_ema = 3'232.0;
    state.slow_ema = 3'333.0;
    state.price_scale = 3'434.0;
    state.alpha_cache = {true, 3'535, 3'636, 3'737, 0.25, 0.5};

    pool.restore_mutable(snapshot);
    require(same_pool_state(pool, expected), "compiled rollback changed pool state");
    require(
        same_compiled_state(pool.policy.compiled_state, expected_policy),
        "compiled rollback changed policy state"
    );
}
#endif

void test_fixed_out_cannot_burn_pool_owned_lp() {
    Pool pool = make_pool(fx::PolicyKind::None);
    pool.add_liquidity({1'000'000.0, 1'000'000.0}, 0.0);
    pool.add_liquidity({50'000.0, 50'000.0}, 0.0, true);
    const Pool before = pool;
    bool threw = false;
    try {
        (void)pool.remove_liquidity_fixed_out(
            pool.single_caller_lp_balance() + 1.0, 0, 1.0, 0.0
        );
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, "fixed-out withdrawal burned pool-owned LP");
    require(same_pool_state(pool, before), "ownership rejection leaked pool state");
}

} // namespace

int main() {
#ifdef TWOCRYPTO_POLICY_HEADER
    test_compiled_policy_rollback();
#endif
    test_fixed_out_cannot_burn_pool_owned_lp();
    return 0;
}
