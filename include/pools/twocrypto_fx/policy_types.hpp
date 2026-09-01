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
    Compiled,
};

inline PolicyKind policy_kind_from_string(const std::string& kind) {
    if (kind.empty() || kind == "none") {
        return PolicyKind::None;
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
    static constexpr std::size_t MAX_POLICY_PARAMS = 64;

    PolicyKind kind{PolicyKind::None};
    // Tunables for repository-owned compiled policies. Unused slots remain
    // zero; n_params records the exact configured width.
    std::array<T, MAX_POLICY_PARAMS> params{};
    std::size_t n_params{0};
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

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
