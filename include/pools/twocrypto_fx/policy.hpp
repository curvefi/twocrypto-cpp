// Value-type external policy facade for the twocrypto simulator.
#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include "policies/compiled.hpp"
#include "policies/native.hpp"
#ifdef TWOCRYPTO_ENABLE_PARITY_POLICIES
#include "policies/fixed.hpp"
#include "policies/sequential.hpp"
#endif

namespace arb {
namespace pools {
namespace twocrypto_fx {

#ifndef TWOCRYPTO_ENABLE_PARITY_POLICIES
template <typename T>
struct ZeroPolicy {
    struct State {};

    static T get_fee(
        const State&, const PolicyConfig<T>&, const PolicyPoolConfig<T>&,
        const PolicyResearchContext<T>&, const std::array<T, 2>&
    ) {
        return T(0);
    }

    static T get_price_scale(
        State&, PolicyResearchContext<T>&, const PolicyConfig<T>&,
        const PolicyPoolConfig<T>&
    ) {
        return T(0);
    }

    static void update_state(
        State&, PolicyResearchContext<T>&, const PolicyConfig<T>&,
        const PolicyPoolConfig<T>&, const PolicyUpdate<T>&
    ) {}
};

template <typename T, int Tag>
struct DisabledParityPolicy {
    struct State {};

    [[noreturn]] static void unavailable() {
        throw std::logic_error("parity-only policy is unavailable in the public SDK");
    }

    static T get_fee(
        const State&, const PolicyConfig<T>&, const PolicyPoolConfig<T>&,
        const PolicyResearchContext<T>&, const std::array<T, 2>&
    ) {
        unavailable();
    }

    static T get_price_scale(
        State&, PolicyResearchContext<T>&, const PolicyConfig<T>&,
        const PolicyPoolConfig<T>&
    ) {
        unavailable();
    }

    static void update_state(
        State&, PolicyResearchContext<T>&, const PolicyConfig<T>&,
        const PolicyPoolConfig<T>&, const PolicyUpdate<T>&
    ) {
        unavailable();
    }
};

template <typename T>
using FixedFeePolicy = DisabledParityPolicy<T, 0>;
template <typename T>
using OracleX2SequentialFeePolicy = DisabledParityPolicy<T, 1>;
#endif

template <typename T, typename PolicyT, typename = void>
struct HasPolicyPriceScaleTarget : std::false_type {};

template <typename T, typename PolicyT, typename = void>
struct HasKeeperAssembledTarget : std::false_type {};

template <typename T, typename PolicyT, typename = void>
struct HasPolicyKeeperDecision : std::false_type {};

template <typename T, typename PolicyT, typename = void>
struct HasPolicyKeeperClockDecision : std::false_type {};

template <typename T, typename PolicyT>
struct HasPolicyPriceScaleTarget<
    T,
    PolicyT,
    std::void_t<decltype(PolicyT::get_price_scale(
        std::declval<typename PolicyT::State&>(),
        std::declval<PolicyResearchContext<T>&>(),
        std::declval<const PolicyConfig<T>&>(),
        std::declval<const PolicyPoolConfig<T>&>()
    ))>
> : std::true_type {};

template <typename T, typename PolicyT>
struct HasKeeperAssembledTarget<
    T,
    PolicyT,
    std::void_t<decltype(PolicyT::keeper_assembled_target(
        std::declval<const typename PolicyT::State&>(),
        std::declval<const PolicyResearchContext<T>&>(),
        std::declval<const PolicyConfig<T>&>(),
        std::declval<const PolicyPoolConfig<T>&>()
    ))>
> : std::true_type {};

template <typename T, typename PolicyT>
struct HasPolicyKeeperDecision<
    T,
    PolicyT,
    std::void_t<decltype(PolicyT::keeper_decision(
        std::declval<const typename PolicyT::State&>(),
        std::declval<const PolicyResearchContext<T>&>(),
        std::declval<const PolicyConfig<T>&>(),
        std::declval<const PolicyPoolConfig<T>&>()
    ))>
> : std::true_type {};

template <typename T, typename PolicyT>
struct HasPolicyKeeperClockDecision<
    T,
    PolicyT,
    std::void_t<decltype(PolicyT::keeper_clock_decision(
        std::declval<const typename PolicyT::State&>(),
        std::declval<const PolicyResearchContext<T>&>(),
        std::declval<const PolicyConfig<T>&>(),
        std::declval<const PolicyPoolConfig<T>&>()
    ))>
> : std::true_type {};

template <typename T, typename PolicyT>
T compiled_price_scale_target_or_zero(
    typename PolicyT::State& state,
    PolicyResearchContext<T>& research,
    const PolicyConfig<T>& params,
    const PolicyPoolConfig<T>& config
) {
    if constexpr (HasPolicyPriceScaleTarget<T, PolicyT>::value) {
        return PolicyT::get_price_scale(state, research, params, config);
    } else {
        return T(0);
    }
}

template <typename T, typename PolicyT>
T compiled_keeper_assembled_target_or_zero(
    const typename PolicyT::State& state,
    const PolicyResearchContext<T>& research,
    const PolicyConfig<T>& params,
    const PolicyPoolConfig<T>& config
) {
    if constexpr (HasKeeperAssembledTarget<T, PolicyT>::value) {
        return PolicyT::keeper_assembled_target(state, research, params, config);
    } else {
        return T(0);
    }
}

template <typename T, typename PolicyT>
PolicyKeeperDecision<T> compiled_policy_keeper_decision_or_empty(
    const typename PolicyT::State& state,
    const PolicyResearchContext<T>& research,
    const PolicyConfig<T>& params,
    const PolicyPoolConfig<T>& config
) {
    if constexpr (HasPolicyKeeperDecision<T, PolicyT>::value) {
        return PolicyT::keeper_decision(state, research, params, config);
    } else {
        return {};
    }
}

template <typename T, typename PolicyT>
PolicyKeeperDecision<T> compiled_policy_keeper_clock_decision_or_empty(
    const typename PolicyT::State& state,
    const PolicyResearchContext<T>& research,
    const PolicyConfig<T>& params,
    const PolicyPoolConfig<T>& config
) {
    if constexpr (HasPolicyKeeperClockDecision<T, PolicyT>::value) {
        return PolicyT::keeper_clock_decision(state, research, params, config);
    } else {
        return compiled_policy_keeper_decision_or_empty<T, PolicyT>(
            state,
            research,
            params,
            config
        );
    }
}

template <typename T>
class PolicyModel {
public:
    PolicyKind kind = PolicyKind::None;
    PolicyConfig<T> params{};
    PolicyPoolConfig<T> config{};
    PolicyResearchContext<T> research{};

    typename ZeroPolicy<T>::State zero_state{};
    typename FixedFeePolicy<T>::State fixed_state{};
    typename NativeFeePolicy<T>::State native_state{};
    typename OracleX2SequentialFeePolicy<T>::State sequence_state{};
    typename ChallengeFeePolicy<T>::State compiled_state{};

    // Transaction-preview rollback needs only the active policy's mutable
    // state. PolicyConfig owns a 1024-element parameter array and
    // PolicyPoolConfig is immutable after pool construction, so copying either
    // object (or inactive policy states) on every speculative route is both
    // unnecessary and expensive.
    using MutableState = std::variant<
        std::monostate,
        typename NativeFeePolicy<T>::State,
        typename OracleX2SequentialFeePolicy<T>::State,
        typename ChallengeFeePolicy<T>::State
    >;

    struct MutableSnapshot {
        PolicyKind kind = PolicyKind::None;
        PolicyResearchContext<T> research{};
        MutableState state{};
    };

    PolicyModel() = default;
    explicit PolicyModel(PolicyKind _kind) : kind(_kind) {
        params.kind = _kind;
    }
    explicit PolicyModel(const PolicyConfig<T>& _params) : kind(_params.kind), params(_params) {}

    MutableSnapshot mutable_snapshot() const {
        MutableSnapshot snapshot{};
        snapshot.kind = kind;
        snapshot.research = research;
        switch (kind) {
        case PolicyKind::None:
        case PolicyKind::ZeroStub:
        case PolicyKind::FixedFee:
            snapshot.state = std::monostate{};
            return snapshot;
        case PolicyKind::TwocryptoPolicy:
            snapshot.state.template emplace<1>(native_state);
            return snapshot;
        case PolicyKind::OracleX2SequentialFee:
            snapshot.state.template emplace<2>(sequence_state);
            return snapshot;
        case PolicyKind::Compiled:
            snapshot.state.template emplace<3>(compiled_state);
            return snapshot;
        }
        throw std::logic_error("unsupported policy kind in mutable snapshot");
    }

    void restore_mutable(const MutableSnapshot& snapshot) {
        if (snapshot.kind != kind) {
            throw std::logic_error("policy kind changed during mutable rollback");
        }
        switch (kind) {
        case PolicyKind::None:
        case PolicyKind::ZeroStub:
        case PolicyKind::FixedFee:
            if (!std::holds_alternative<std::monostate>(snapshot.state)) {
                throw std::logic_error("invalid empty policy snapshot state");
            }
            break;
        case PolicyKind::TwocryptoPolicy:
            native_state = std::get<1>(snapshot.state);
            break;
        case PolicyKind::OracleX2SequentialFee:
            sequence_state = std::get<2>(snapshot.state);
            break;
        case PolicyKind::Compiled:
            compiled_state = std::get<3>(snapshot.state);
            break;
        default:
            throw std::logic_error("unsupported policy kind in mutable restore");
        }
        research = snapshot.research;
    }

    // Quote results are cacheable only for native no-policy pools and for the
    // compiled passthrough selected when no repository policy header is built.
    bool quote_cache_safe() const {
        switch (kind) {
        case PolicyKind::None:
            return true;
        case PolicyKind::Compiled:
            return compiled_detail::quote_cache_safe;
        default:
            return false;
        }
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

    // Fee quotes need current time even when the tweak-price gates stay closed.
    void set_block_timestamp(uint64_t block_timestamp) {
        research.block_timestamp = block_timestamp;
    }

    // Lower bound on any fee get_fee can return under the current config.
    // `native_floor` is the pool's own native-fee floor, returned for kinds
    // that fall through to the native fee (a 0 from get_fee means "use the
    // native fee" in the pool). Errs low: 0 = unknown, caller clamps.
    T fee_floor(const T& native_floor) const {
        switch (kind) {
        case PolicyKind::None:
        case PolicyKind::ZeroStub:
        case PolicyKind::TwocryptoPolicy:
            return native_floor;
        case PolicyKind::FixedFee:
            return params.fee != T(0) ? params.fee : native_floor;
        case PolicyKind::Compiled:
            // The selected policy owns its conservative fee floor.
            return ChallengeFeePolicy<T>::fee_floor(params, config, native_floor);
        default:
            return T(0);
        }
    }

    T get_fee(const std::array<T, 2>& xp) const {
        switch (kind) {
        case PolicyKind::FixedFee:
            return FixedFeePolicy<T>::get_fee(fixed_state, params, config, research, xp);
        case PolicyKind::OracleX2SequentialFee:
            return OracleX2SequentialFeePolicy<T>::get_fee(sequence_state, params, config, research, xp);
        case PolicyKind::TwocryptoPolicy:
            return NativeFeePolicy<T>::get_fee(native_state, params, config, research, xp);
        case PolicyKind::Compiled:
            return ChallengeFeePolicy<T>::get_fee(compiled_state, params, config, research, xp);
        case PolicyKind::None:
        case PolicyKind::ZeroStub:
            return ZeroPolicy<T>::get_fee(zero_state, params, config, research, xp);
        }
        return T(0);
    }

    T get_price_scale() {
        switch (kind) {
        case PolicyKind::OracleX2SequentialFee:
            return OracleX2SequentialFeePolicy<T>::get_price_scale(
                sequence_state,
                research,
                params,
                config
            );
        case PolicyKind::TwocryptoPolicy:
            return NativeFeePolicy<T>::get_price_scale(native_state, research, params, config);
        case PolicyKind::FixedFee:
            return FixedFeePolicy<T>::get_price_scale(fixed_state, research, params, config);
        case PolicyKind::Compiled:
            return compiled_price_scale_target_or_zero<T, ChallengeFeePolicy<T>>(
                compiled_state,
                research,
                params,
                config
            );
        case PolicyKind::None:
        case PolicyKind::ZeroStub:
            return ZeroPolicy<T>::get_price_scale(zero_state, research, params, config);
        }
        return T(0);
    }

    // Read-only target projection for off-chain keeper preflight. Copy only
    // the active policy's small mutable state and research overlay; the large
    // parameter/config objects remain shared const inputs.
    T preview_price_scale_at(
        uint64_t block_timestamp,
        const T& current_price_oracle
    ) const {
        PolicyResearchContext<T> view_research = research;
        view_research.block_timestamp = block_timestamp;
        view_research.price_oracle = current_price_oracle;
        switch (kind) {
        case PolicyKind::OracleX2SequentialFee: {
            auto state = sequence_state;
            return OracleX2SequentialFeePolicy<T>::get_price_scale(
                state,
                view_research,
                params,
                config
            );
        }
        case PolicyKind::TwocryptoPolicy: {
            auto state = native_state;
            return NativeFeePolicy<T>::get_price_scale(
                state,
                view_research,
                params,
                config
            );
        }
        case PolicyKind::FixedFee: {
            auto state = fixed_state;
            return FixedFeePolicy<T>::get_price_scale(
                state,
                view_research,
                params,
                config
            );
        }
        case PolicyKind::Compiled: {
            auto state = compiled_state;
            return compiled_price_scale_target_or_zero<T, ChallengeFeePolicy<T>>(
                state,
                view_research,
                params,
                config
            );
        }
        case PolicyKind::None:
        case PolicyKind::ZeroStub: {
            auto state = zero_state;
            return ZeroPolicy<T>::get_price_scale(
                state,
                view_research,
                params,
                config
            );
        }
        }
        return T(0);
    }

    // Raw post-policy target used only to decide whether a scheduleless
    // keeper should attempt a speculative poke.
    T keeper_assembled_target() const {
        if (kind == PolicyKind::Compiled) {
            return compiled_keeper_assembled_target_or_zero<T, ChallengeFeePolicy<T>>(
                compiled_state,
                research,
                params,
                config
            );
        }
        return T(0);
    }

    PolicyKeeperDecision<T> keeper_decision() const {
        if (kind == PolicyKind::Compiled) {
            return compiled_policy_keeper_decision_or_empty<T, ChallengeFeePolicy<T>>(
                compiled_state,
                research,
                params,
                config
            );
        }
        return {};
    }

    PolicyKeeperDecision<T> keeper_decision_at(
        uint64_t block_timestamp,
        const T& current_price_oracle
    ) const {
        if (kind != PolicyKind::Compiled) return {};
        // Do not copy PolicyModel here: PolicyConfig owns a 1024-element
        // parameter array. A keeper view needs only a small research-context
        // overlay while state/config/params remain const references.
        PolicyResearchContext<T> view_research = research;
        view_research.block_timestamp = block_timestamp;
        view_research.price_oracle = current_price_oracle;
        return compiled_policy_keeper_decision_or_empty<T, ChallengeFeePolicy<T>>(
            compiled_state,
            view_research,
            params,
            config
        );
    }

    PolicyKeeperDecision<T> keeper_clock_decision_at(
        uint64_t block_timestamp
    ) const {
        if (kind != PolicyKind::Compiled) return {};
        PolicyResearchContext<T> view_research = research;
        view_research.block_timestamp = block_timestamp;
        return compiled_policy_keeper_clock_decision_or_empty<
            T,
            ChallengeFeePolicy<T>
        >(
            compiled_state,
            view_research,
            params,
            config
        );
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
        PolicyUpdate<T> update{
            xp,
            price_scale,
            price_oracle,
            last_prices,
            virtual_price,
            xcp_profit,
            d_value,
            oracle_timestamp
        };

        switch (kind) {
        case PolicyKind::FixedFee:
            FixedFeePolicy<T>::update_state(fixed_state, research, params, config, update);
            return;
        case PolicyKind::OracleX2SequentialFee:
            OracleX2SequentialFeePolicy<T>::update_state(sequence_state, research, params, config, update);
            return;
        case PolicyKind::TwocryptoPolicy:
            NativeFeePolicy<T>::update_state(native_state, research, params, config, update);
            return;
        case PolicyKind::Compiled:
            ChallengeFeePolicy<T>::update_state(compiled_state, research, params, config, update);
            return;
        case PolicyKind::None:
        case PolicyKind::ZeroStub:
            ZeroPolicy<T>::update_state(zero_state, research, params, config, update);
            return;
        }
    }
};

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
