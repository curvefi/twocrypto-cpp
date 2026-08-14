// Deterministic sequential-fee policy used for parity tests.
#pragma once

#include "common.hpp"

namespace arb {
namespace pools {
namespace twocrypto_fx {

template <typename T>
struct OracleX2SequentialFeePolicy {
    struct State {
        PolicyPoolSnapshot<T> pool{};
        T policy_fee{T(0)};
        uint64_t update_nonce{0};
    };

    static constexpr uint64_t MIN_SEQUENCE_FEE_BPS = 1ULL;
    static constexpr uint64_t MAX_SEQUENCE_FEE_BPS = 1000ULL;
    static constexpr uint64_t SEQUENCE_CYCLE_LENGTH = 100ULL;
    static constexpr uint64_t SEQUENCE_HALF_CYCLE = 50ULL;

    static T get_fee(
        const State& state,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config,
        const PolicyResearchContext<T>& research,
        const std::array<T, 2>& xp
    ) {
        (void)params;
        (void)config;
        (void)research;
        (void)xp;
        return state.policy_fee;
    }

    static T get_price_scale(
        State& state,
        PolicyResearchContext<T>& research,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config
    ) {
        (void)research;
        (void)params;
        (void)config;
        // Mirrors the deployable policy: pool views cannot be re-entered from
        // get_price_scale(), so the target uses the last committed snapshot.
        if (state.pool.ts == 0) {
            return T(0);
        }
        return state.pool.price_oracle + state.pool.price_oracle;
    }

    static void update_state(
        State& state,
        PolicyResearchContext<T>& research,
        const PolicyConfig<T>& params,
        const PolicyPoolConfig<T>& config,
        const PolicyUpdate<T>& update
    ) {
        (void)research;
        (void)params;
        store_policy_snapshot(state.pool, update);

        state.update_nonce += 1;
        uint64_t fee_bps = sequence_fee_bps(state.update_nonce);
        state.policy_fee = T(fee_bps) * config.fee_precision / T(POLICY_BPS_SCALE);
    }

private:
    static uint64_t sequence_fee_bps(uint64_t update_nonce) {
        if (update_nonce == 0) {
            return 0;
        }
        uint64_t idx = (update_nonce - 1) % SEQUENCE_CYCLE_LENGTH;
        uint64_t leg = (idx < SEQUENCE_HALF_CYCLE)
            ? idx
            : (SEQUENCE_CYCLE_LENGTH - 1 - idx);
        return MIN_SEQUENCE_FEE_BPS + (
            leg * (MAX_SEQUENCE_FEE_BPS - MIN_SEQUENCE_FEE_BPS)
        ) / (SEQUENCE_HALF_CYCLE - 1);
    }
};

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
