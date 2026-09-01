// Value-type external policy facade for the twocrypto simulator.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>

#include "policies/compiled.hpp"

namespace arb {
namespace pools {
namespace twocrypto_fx {

template <typename T>
class PolicyModel {
public:
    PolicyKind kind = PolicyKind::None;
    PolicyConfig<T> params{};
    PolicyPoolConfig<T> config{};
    PolicyResearchContext<T> research{};

#ifdef TWOCRYPTO_POLICY_HEADER
    typename ChallengeFeePolicy<T>::State compiled_state{};
#endif

    struct MutableSnapshot {
        PolicyKind kind = PolicyKind::None;
        PolicyResearchContext<T> research{};
#ifdef TWOCRYPTO_POLICY_HEADER
        std::optional<typename ChallengeFeePolicy<T>::State> compiled_state{};
#endif
    };

    PolicyModel() = default;

    explicit PolicyModel(PolicyKind policy_kind) : kind(policy_kind) {
        params.kind = policy_kind;
        require_available_kind();
    }

    explicit PolicyModel(const PolicyConfig<T>& policy_params)
        : kind(policy_params.kind), params(policy_params) {
        require_available_kind();
    }

    MutableSnapshot mutable_snapshot() const {
        MutableSnapshot snapshot{};
        snapshot.kind = kind;
        snapshot.research = research;
        switch (kind) {
        case PolicyKind::None:
            return snapshot;
        case PolicyKind::Compiled:
#ifdef TWOCRYPTO_POLICY_HEADER
            snapshot.compiled_state = compiled_state;
            return snapshot;
#else
            break;
#endif
        }
        throw std::logic_error("unsupported policy kind in mutable snapshot");
    }

    void restore_mutable(const MutableSnapshot& snapshot) {
        if (snapshot.kind != kind) {
            throw std::logic_error("policy kind changed during mutable rollback");
        }
        switch (kind) {
        case PolicyKind::None:
            break;
        case PolicyKind::Compiled:
#ifdef TWOCRYPTO_POLICY_HEADER
            if (!snapshot.compiled_state) {
                throw std::logic_error("missing compiled policy snapshot state");
            }
            compiled_state = *snapshot.compiled_state;
            break;
#else
            throw std::logic_error("compiled policy is unavailable");
#endif
        default:
            throw std::logic_error("unsupported policy kind in mutable restore");
        }
        research = snapshot.research;
    }

    void configure_pool(
        const T& mid_fee,
        const T& out_fee,
        const T& fee_gamma,
        const T& ma_time,
        const T& precision,
        const T& fee_precision,
        const T& A,
        const T& gamma,
        const T& adjustment_step_min,
        const T& adjustment_step_max
    ) {
        config.A = A;
        config.gamma = gamma;
        config.mid_fee = mid_fee;
        config.out_fee = out_fee;
        config.fee_gamma = fee_gamma;
        config.ma_time = ma_time;
        config.precision = precision;
        config.fee_precision = fee_precision;
        config.adjustment_step_min = adjustment_step_min;
        config.adjustment_step_max = adjustment_step_max;
    }

    void prepare_price_scale_call(uint64_t block_timestamp, const T& price_oracle) {
        research.block_timestamp = block_timestamp;
        research.price_oracle = price_oracle;
    }

    void set_block_timestamp(uint64_t block_timestamp) {
        research.block_timestamp = block_timestamp;
    }

    // A zero policy fee delegates to the pool's native fee. The compiled
    // policy owns any tighter conservative floor it advertises.
    T fee_floor(const T& native_floor) const {
        switch (kind) {
        case PolicyKind::None:
            return native_floor;
        case PolicyKind::Compiled:
#ifdef TWOCRYPTO_POLICY_HEADER
            return ChallengeFeePolicy<T>::fee_floor(params, config, native_floor);
#else
            break;
#endif
        }
        throw std::logic_error("unsupported policy kind in fee floor");
    }

    T get_fee(const std::array<T, 2>& xp) const {
        switch (kind) {
        case PolicyKind::None:
            return T(0);
        case PolicyKind::Compiled:
#ifdef TWOCRYPTO_POLICY_HEADER
            return ChallengeFeePolicy<T>::get_fee(
                compiled_state, params, config, research, xp
            );
#else
            break;
#endif
        }
        throw std::logic_error("unsupported policy kind in fee hook");
    }

    T get_price_scale() {
        switch (kind) {
        case PolicyKind::None:
            return T(0);
        case PolicyKind::Compiled:
#ifdef TWOCRYPTO_POLICY_HEADER
            return ChallengeFeePolicy<T>::get_price_scale(
                compiled_state, research, params, config
            );
#else
            break;
#endif
        }
        throw std::logic_error("unsupported policy kind in price-scale hook");
    }

    void update_state(
        const std::array<T, 2>& xp,
        const T& price_scale,
        const T& price_oracle,
        const T& last_prices,
        const T& virtual_price,
        const T& xcp_profit,
        const T& d_value,
        uint64_t oracle_timestamp
    ) {
        if (kind == PolicyKind::None) return;
        if (kind != PolicyKind::Compiled) {
            throw std::logic_error("unsupported policy kind in state hook");
        }
#ifdef TWOCRYPTO_POLICY_HEADER
        const PolicyUpdate<T> update{
            xp,
            price_scale,
            price_oracle,
            last_prices,
            virtual_price,
            xcp_profit,
            d_value,
            oracle_timestamp
        };
        ChallengeFeePolicy<T>::update_state(
            compiled_state, research, params, config, update
        );
#else
        throw std::logic_error("compiled policy is unavailable");
#endif
    }

private:
    void require_available_kind() const {
        switch (kind) {
        case PolicyKind::None:
            return;
        case PolicyKind::Compiled:
#ifdef TWOCRYPTO_POLICY_HEADER
            return;
#else
            throw std::invalid_argument(
                "compiled policy requires TWOCRYPTO_POLICY_HEADER"
            );
#endif
        }
        throw std::invalid_argument("unsupported policy kind");
    }
};

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
