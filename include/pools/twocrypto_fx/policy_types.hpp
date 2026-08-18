// Shared types for twocrypto simulator policy models.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "stableswap_math.hpp"

namespace arb {
namespace pools {
namespace twocrypto_fx {

inline constexpr uint64_t POLICY_BPS_SCALE = 10000ULL;

enum class PolicyKind {
    None,
    TwocryptoPolicy,
    ZeroStub,
    OracleX2SequentialFee,
    FixedFee,
    Compiled,
};

inline PolicyKind policy_kind_from_string(const std::string& kind) {
    if (kind.empty() || kind == "none") {
        return PolicyKind::None;
    }
    if (kind == "twocrypto_policy") {
        return PolicyKind::TwocryptoPolicy;
    }
    if (kind == "zero_stub") {
#ifndef TWOCRYPTO_ENABLE_PARITY_POLICIES
        throw std::invalid_argument("zero_stub is a parity-only policy kind");
#else
        return PolicyKind::ZeroStub;
#endif
    }
    if (
        kind == "oracle_x2" ||
        kind == "price_oracle_x2" ||
        kind == "oracle_x2_sequential_fee"
    ) {
#ifndef TWOCRYPTO_ENABLE_PARITY_POLICIES
        throw std::invalid_argument("oracle_x2 is a parity-only policy kind");
#else
        return PolicyKind::OracleX2SequentialFee;
#endif
    }
    if (kind == "fixed_fee" || kind == "fixed_fee_policy") {
#ifndef TWOCRYPTO_ENABLE_PARITY_POLICIES
        throw std::invalid_argument("fixed_fee is a parity-only policy kind");
#else
        return PolicyKind::FixedFee;
#endif
    }
    if (kind == "compiled") {
#ifdef TWOCRYPTO_POLICY_HEADER
        return PolicyKind::Compiled;
#else
        throw std::invalid_argument(
            "compiled policy kind is unavailable in this fixed harness"
        );
#endif
    }
    throw std::invalid_argument("unsupported policy kind: " + kind);
}

template <typename T>
struct PolicyConfig {
    static constexpr std::size_t MAX_POLICY_PARAMS = 1024;

    PolicyKind kind{PolicyKind::None};
    T fee{T(0)};
    // Tunables for repository-owned compiled policies. Unused slots remain
    // zero; n_params records the exact configured width.
    std::array<T, MAX_POLICY_PARAMS> params{};
    std::size_t n_params{0};
};

template <typename T>
struct PolicyPoolSnapshot {
    std::array<T, 2> xp{T(0), T(0)};
    T price_scale{T(0)};
    T price_oracle{T(0)};
    T last_prices{T(0)};
    T virtual_price{T(0)};
    T xcp_profit{T(0)};
    T D{T(0)};
    uint64_t ts{0};
};

template <typename T>
struct PolicyPoolConfig {
    T A{T(0)};
    T gamma{T(0)};
    T mid_fee{T(0)};
    T out_fee{T(0)};
    T fee_gamma{T(0)};
    T ma_time{T(1)};
    T precision{T(1)};
    T fee_precision{T(1)};
    // Immutable actuator constants exposed to view-only keeper triggers. This
    // predicts p_new without copying the pool or running invariant math.
    T adjustment_step_min{T(0)};
    T adjustment_step_max{T(0)};
};

template <typename T>
struct PolicyResearchContext {
    uint64_t block_timestamp{0};
    T price_oracle{T(0)};
};

template <typename T>
struct PolicyUpdate {
    const std::array<T, 2>& xp;
    const T& price_scale;
    const T& price_oracle;
    const T& last_prices;
    const T& virtual_price;
    const T& xcp_profit;
    const T& D;
    uint64_t oracle_timestamp;
};

// View-shaped liveness decision returned by policies which own their keeper
// trigger. The policy decides only whether a poke is worth attempting; the
// pool's real tweak_price path remains the authority on LP/burn feasibility.
template <typename T>
struct PolicyKeeperDecision {
    bool available{false};
    bool ready{false};
    T raw_target{T(0)};
    // Exact policy target to pass into the pool actuator when ready. Keeping
    // this in the decision prevents a view-time readiness calculation and a
    // later get_price_scale() call from projecting different EMA states.
    T keeper_target{T(0)};
    T raw_gap_bps{T(0)};
    T shaped_target{T(0)};
    T predicted_price_scale{T(0)};
    T effective_step_bps{T(0)};
    // Generic trigger telemetry. Raw/effective-gap keep bps units; stateful
    // integral triggers use bps*seconds without overloading the legacy names.
    T trigger_value{T(0)};
    T trigger_threshold{T(0)};
    bool trigger_value_is_bps{true};
    T trigger_value_bps{T(0)};
    T threshold_bps{T(0)};
    // Optional actuator diagnostics for policies which alter how far a
    // triggered keeper closes. These fields are observational only: the
    // already-computed shaped_target remains the sole actuator input.
    bool has_closure_context{false};
    bool closure_active{false};
    bool closure_direction_ok{false};
    bool closure_deadbanded{false};
    bool closure_applies{false};
    bool closure_effective{false};
    bool closure_realized_effective{false};
    bool closure_base_cap_bound{false};
    bool closure_boost_cap_bound{false};
    T closure_unboosted_target{T(0)};
    T closure_unclamped_boosted_target{T(0)};
    T closure_unboosted_predicted_price_scale{T(0)};
    uint64_t min_interval_s{0};
    uint64_t last_price_scale_change_ts{0};
    uint64_t next_allowed_ts{0};
};

template <typename T>
inline void store_policy_snapshot(PolicyPoolSnapshot<T>& snapshot, const PolicyUpdate<T>& update) {
    snapshot.xp = update.xp;
    snapshot.price_scale = update.price_scale;
    snapshot.price_oracle = update.price_oracle;
    snapshot.last_prices = update.last_prices;
    snapshot.virtual_price = update.virtual_price;
    snapshot.xcp_profit = update.xcp_profit;
    snapshot.D = update.D;
    snapshot.ts = update.oracle_timestamp;
}

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
