#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <tuple>

#include "pools/twocrypto_fx/twocrypto.hpp"

namespace fx = arb::pools::twocrypto_fx;
using Pool = fx::TwoCryptoPool<double>;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Pool make_pool(fx::PolicyKind kind) {
    return Pool(
        {1.0, 1.0},
        50'000.0,
        1e-4,
        1e-3,
        2e-3,
        1e-2,
        1e-10,
        5e-3,
        865.0,
        1.0,
        0.5,
        0.5,
        kind
    );
}

bool same_policy_pool(
    const fx::PolicyPoolSnapshot<double>& a,
    const fx::PolicyPoolSnapshot<double>& b
) {
    return a.xp == b.xp &&
        a.price_scale == b.price_scale &&
        a.price_oracle == b.price_oracle &&
        a.last_prices == b.last_prices &&
        a.virtual_price == b.virtual_price &&
        a.xcp_profit == b.xcp_profit &&
        a.D == b.D &&
        a.ts == b.ts;
}

bool same_metrics(
    const fx::PolicyHookMetrics<double>& a,
    const fx::PolicyHookMetrics<double>& b
) {
    return std::tie(
        a.target_outer_eval_count,
        a.target_outer_ready_count,
        a.target_outer_blocked_profit_count,
        a.target_outer_blocked_same_block_count,
        a.target_eval_count,
        a.target_policy_nonzero_count,
        a.target_step_move_count,
        a.target_step_min_veto_count,
        a.target_step_noop_count,
        a.target_lp_veto_count,
        a.target_lp_veto_below_precision_count,
        a.target_lp_veto_below_lp_xcp_profit_count,
        a.target_lp_veto_burn_cap_exhausted_count,
        a.target_donation_burn_used_count,
        a.target_donation_burn_cap_exhausted_count,
        a.target_commit_count
    ) == std::tie(
        b.target_outer_eval_count,
        b.target_outer_ready_count,
        b.target_outer_blocked_profit_count,
        b.target_outer_blocked_same_block_count,
        b.target_eval_count,
        b.target_policy_nonzero_count,
        b.target_step_move_count,
        b.target_step_min_veto_count,
        b.target_step_noop_count,
        b.target_lp_veto_count,
        b.target_lp_veto_below_precision_count,
        b.target_lp_veto_below_lp_xcp_profit_count,
        b.target_lp_veto_burn_cap_exhausted_count,
        b.target_donation_burn_used_count,
        b.target_donation_burn_cap_exhausted_count,
        b.target_commit_count
    );
}

bool same_mutable_state(const Pool& a, const Pool& b) {
    return a.balances == b.balances &&
        a.D == b.D &&
        a.totalSupply == b.totalSupply &&
        a.cached_price_scale == b.cached_price_scale &&
        a.cached_price_oracle == b.cached_price_oracle &&
        a.last_prices == b.last_prices &&
        a.last_timestamp == b.last_timestamp &&
        a.cached_ema_dt == b.cached_ema_dt &&
        a.cached_ema_alpha == b.cached_ema_alpha &&
        a.cached_ema_alpha_valid == b.cached_ema_alpha_valid &&
        a.xcp_profit == b.xcp_profit &&
        a.lp_xcp_profit == b.lp_xcp_profit &&
        a.virtual_price == b.virtual_price &&
        a.admin_balances == b.admin_balances &&
        a.last_admin_fee_claim_timestamp ==
            b.last_admin_fee_claim_timestamp &&
        a.block_timestamp == b.block_timestamp &&
        a.donation_shares == b.donation_shares &&
        a.last_donation_release_ts == b.last_donation_release_ts &&
        a.donation_protection_expiry_ts == b.donation_protection_expiry_ts &&
        a.donation_protection_extension_remainder ==
            b.donation_protection_extension_remainder &&
        a.policy.research.block_timestamp == b.policy.research.block_timestamp &&
        a.policy.research.price_oracle == b.policy.research.price_oracle &&
        same_policy_pool(a.policy.native_state.pool, b.policy.native_state.pool) &&
        same_policy_pool(a.policy.sequence_state.pool, b.policy.sequence_state.pool) &&
        a.policy.sequence_state.policy_fee == b.policy.sequence_state.policy_fee &&
        a.policy.sequence_state.update_nonce == b.policy.sequence_state.update_nonce &&
        same_metrics(a.policy_hook_metrics, b.policy_hook_metrics);
}

void mutate_every_captured_category(Pool& pool) {
    pool.balances = {101.0, 202.0};
    pool.D = 303.0;
    pool.totalSupply = 404.0;
    pool.cached_price_scale = 505.0;
    pool.cached_price_oracle = 606.0;
    pool.last_prices = 707.0;
    pool.last_timestamp = 808;
    pool.cached_ema_dt = 909;
    pool.cached_ema_alpha = 0.125;
    pool.cached_ema_alpha_valid = !pool.cached_ema_alpha_valid;
    pool.xcp_profit = 1'010.0;
    pool.lp_xcp_profit = 1'111.0;
    pool.virtual_price = 1'212.0;
    pool.admin_balances = {1'313.0, 1'414.0};
    pool.last_admin_fee_claim_timestamp = 1'415;
    pool.block_timestamp = 1'515;
    pool.donation_shares = 1'616.0;
    pool.last_donation_release_ts = 1'717.0;
    pool.donation_protection_expiry_ts = 1'818.0;
    pool.donation_protection_extension_remainder = 1'919.0;

    pool.policy.research.block_timestamp = 2'020;
    pool.policy.research.price_oracle = 2'121.0;
    // The native state is inactive for this sequential-policy snapshot and is
    // intentionally left unchanged here; a dedicated test proves that it is
    // not copied on rollback.
    pool.policy.sequence_state.pool.xp = {3'131.0, 3'232.0};
    pool.policy.sequence_state.pool.price_scale = 3'333.0;
    pool.policy.sequence_state.pool.price_oracle = 3'434.0;
    pool.policy.sequence_state.pool.last_prices = 3'535.0;
    pool.policy.sequence_state.pool.virtual_price = 3'636.0;
    pool.policy.sequence_state.pool.xcp_profit = 3'737.0;
    pool.policy.sequence_state.pool.D = 3'838.0;
    pool.policy.sequence_state.pool.ts = 3'939;
    pool.policy.sequence_state.policy_fee = 0.004;
    pool.policy.sequence_state.update_nonce = 4'040;

    pool.policy_hook_metrics.target_outer_eval_count = 4'141;
    pool.policy_hook_metrics.target_outer_ready_count = 4'242;
    pool.policy_hook_metrics.target_outer_blocked_profit_count = 4'343;
    pool.policy_hook_metrics.target_outer_blocked_same_block_count = 4'444;
    pool.policy_hook_metrics.target_eval_count = 4'545;
    pool.policy_hook_metrics.target_policy_nonzero_count = 4'646;
    pool.policy_hook_metrics.target_step_move_count = 4'747;
    pool.policy_hook_metrics.target_step_min_veto_count = 4'848;
    pool.policy_hook_metrics.target_step_noop_count = 4'949;
    pool.policy_hook_metrics.target_lp_veto_count = 5'050;
    pool.policy_hook_metrics.target_lp_veto_below_precision_count = 5'151;
    pool.policy_hook_metrics.target_lp_veto_below_lp_xcp_profit_count = 5'252;
    pool.policy_hook_metrics.target_lp_veto_burn_cap_exhausted_count = 5'353;
    pool.policy_hook_metrics.target_donation_burn_used_count = 5'454;
    pool.policy_hook_metrics.target_donation_burn_cap_exhausted_count = 5'555;
    pool.policy_hook_metrics.target_commit_count = 5'656;
}

void test_mutable_snapshot_round_trip() {
    Pool pool = make_pool(fx::PolicyKind::OracleX2SequentialFee);
    pool.add_liquidity({1'000'000.0, 1'000'000.0}, 0.0);
    pool.set_block_timestamp(1234);
    pool.policy.sequence_state.policy_fee = 0.003;
    pool.policy.sequence_state.update_nonce = 17;
    pool.policy.research.price_oracle = 1.25;
    pool.policy_hook_metrics.target_eval_count = 9;

    const Pool expected = pool;
    const auto snapshot = pool.mutable_snapshot();
    mutate_every_captured_category(pool);
    require(!same_mutable_state(pool, expected), "snapshot mutation setup was ineffective");

    pool.restore_mutable(snapshot);
    require(same_mutable_state(pool, expected), "mutable snapshot failed to restore state");
}

void test_active_policy_snapshot_variants() {
    Pool none = make_pool(fx::PolicyKind::None);
    none.policy.research.block_timestamp = 17;
    none.policy.research.price_oracle = 1.5;
    const auto none_snapshot = none.mutable_snapshot();
    none.policy.research.block_timestamp = 99;
    none.policy.research.price_oracle = 9.5;
    none.restore_mutable(none_snapshot);
    require(
        none.policy.research.block_timestamp == 17 &&
            none.policy.research.price_oracle == 1.5,
        "none policy rollback failed to restore research context"
    );

    Pool compiled = make_pool(fx::PolicyKind::Compiled);
    compiled.policy.research.block_timestamp = 23;
    compiled.policy.research.price_oracle = 2.5;
    const auto compiled_snapshot = compiled.mutable_snapshot();
    require(
        compiled_snapshot.policy.kind == fx::PolicyKind::Compiled,
        "compiled policy snapshot lost its active-state tag"
    );
    compiled.policy.research.block_timestamp = 101;
    compiled.policy.research.price_oracle = 10.5;
    compiled.restore_mutable(compiled_snapshot);
    require(
        compiled.policy.research.block_timestamp == 23 &&
            compiled.policy.research.price_oracle == 2.5,
        "compiled policy rollback failed to restore research context"
    );
}

void test_inactive_policy_state_is_not_snapshotted() {
    Pool pool = make_pool(fx::PolicyKind::OracleX2SequentialFee);
    const auto snapshot = pool.mutable_snapshot();
    pool.policy.native_state.pool.xp = {7.0, 8.0};
    pool.policy.sequence_state.policy_fee = 0.004;
    pool.restore_mutable(snapshot);
    require(
        pool.policy.native_state.pool.xp == std::array<double, 2>{7.0, 8.0},
        "rollback copied inactive native policy state"
    );
    require(
        pool.policy.sequence_state.policy_fee == 0.0,
        "rollback failed to restore active sequential policy state"
    );
}

void test_unsupported_policy_snapshot_kind_rejects() {
    Pool pool = make_pool(fx::PolicyKind::None);
    pool.policy.kind = static_cast<fx::PolicyKind>(99);
    bool threw = false;
    try {
        (void)pool.mutable_snapshot();
    } catch (const std::logic_error&) {
        threw = true;
    }
    require(threw, "unsupported policy kind silently fell through snapshot");
}

void test_quote_cache_safety_matrix() {
    Pool none = make_pool(fx::PolicyKind::None);
    Pool compiled = make_pool(fx::PolicyKind::Compiled);
    require(none.quote_cache_safe(), "none policy must be cache-safe");
    require(none.uses_native_fee_model(), "none policy must use native fee model");
    require(
        &none.hook_metrics() == &none.policy_hook_metrics,
        "hook metrics accessor must expose pool-owned metrics"
    );
    require(
#if defined(TWOCRYPTO_POLICY_HEADER)
        !compiled.quote_cache_safe(),
        "selected compiled policy must not be cache-safe"
#else
        compiled.quote_cache_safe(),
        "default compiled passthrough must be cache-safe"
#endif
    );
    require(
        !compiled.uses_native_fee_model(),
        "compiled policy must not use native fee model"
    );
    require(
        !make_pool(fx::PolicyKind::ZeroStub).quote_cache_safe(),
        "zero stub must not be cache-safe"
    );
    require(
        !make_pool(fx::PolicyKind::TwocryptoPolicy).quote_cache_safe(),
        "native policy must not be cache-safe"
    );
    require(
        !make_pool(fx::PolicyKind::OracleX2SequentialFee).quote_cache_safe(),
        "sequential policy must not be cache-safe"
    );
    require(
        !make_pool(fx::PolicyKind::FixedFee).quote_cache_safe(),
        "fixed policy must not be cache-safe"
    );
}

void test_failed_add_is_atomic() {
    Pool pool = make_pool(fx::PolicyKind::None);
    pool.add_liquidity({1'000'000.0, 1'000'000.0}, 0.0);
    const Pool before = pool;
    double charged_lp_fee = 123.0;

    try {
        (void)pool.add_liquidity(
            {100'000.0, 0.0},
            std::numeric_limits<double>::max(),
            false,
            &charged_lp_fee
        );
        throw std::runtime_error("failed add unexpectedly succeeded");
    } catch (const std::runtime_error&) {
    }

    require(
        same_mutable_state(pool, before),
        "failed add_liquidity leaked mutable state"
    );
    require(
        charged_lp_fee == 123.0,
        "failed add_liquidity changed its fee output"
    );
}

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
    require(
        same_mutable_state(pool, before),
        "failed fixed-out ownership check leaked mutable state"
    );
}

} // namespace

int main() {
    test_mutable_snapshot_round_trip();
    test_active_policy_snapshot_variants();
    test_inactive_policy_state_is_not_snapshotted();
    test_unsupported_policy_snapshot_kind_rejects();
    test_quote_cache_safety_matrix();
    test_failed_add_is_atomic();
    test_fixed_out_cannot_burn_pool_owned_lp();
    return 0;
}
