// Value-type external policy facade for the twocrypto simulator.
#pragma once

#include <array>
#include <cmath>
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

    T get_liquidity_fee(const std::array<T,2>& xp, const T& live_D) const {
#ifdef TWOCRYPTO_POLICY_HEADER
        if constexpr (compiled_detail::HasLiquidityFee<ChallengeFeePolicy<T>, T>::value) {
            if (kind == PolicyKind::Compiled)
                return ChallengeFeePolicy<T>::get_liquidity_fee(
                    compiled_state, params, config, research, xp, live_D);
        }
#endif
        return get_fee(xp, live_D);
    }

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
#ifdef TWOCRYPTO_POLICY_HEADER
        if (kind == PolicyKind::Compiled) {
            ChallengeFeePolicy<T>::validate_params(params, config);
        }
#endif
    }

    void prepare_price_scale_call(uint64_t block_timestamp, const T& price_oracle) {
        research.block_timestamp = block_timestamp;
        research.price_oracle = price_oracle;
    }

    void set_price_feed(const T& price, double timestamp) {
        if (!(price > T(0))) {
            throw std::invalid_argument("policy price feed must be positive");
        }
        if (!std::isfinite(timestamp) || timestamp < 0) {
            throw std::invalid_argument("policy price feed timestamp must be finite and nonnegative");
        }
        // Reports may lead the block by up to 60 s; the policy owns admission.
        if (timestamp > static_cast<double>(research.block_timestamp) + 60.0) {
            throw std::invalid_argument("policy price feed is more than 60 s after the block");
        }
        research.price_feed = price;
        research.price_feed_timestamp = timestamp;
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

    // Optional policy hook: bound every size in this input direction while
    // the current pool state and report context remain fixed. Older policies
    // retain their global bound; a spot fee is not a valid substitute.
    T context_fee_floor(const T& native_floor, size_t input_coin) const {
        return context_fee_floor(research, native_floor, input_coin);
    }

    // Explicit-context form: lets an event cursor bound a hypothetical report
    // without changing the live research context.
    T context_fee_floor(
        [[maybe_unused]] const PolicyResearchContext<T>& context,
        const T& native_floor, [[maybe_unused]] size_t input_coin
    ) const {
#ifdef TWOCRYPTO_POLICY_HEADER
        if constexpr (compiled_detail::HasContextFeeFloor<ChallengeFeePolicy<T>, T>::value) {
            if (kind == PolicyKind::Compiled) {
                return ChallengeFeePolicy<T>::context_fee_floor(
                    compiled_state, params, config, context, input_coin);
            }
        }
#endif
        return fee_floor(native_floor);
    }

    // Optional policy hook: false only when no swap from input_coin can profit
    // against `external` (net of external fees) in this context. Policies
    // without the hook never reject, so callers keep sizing every opportunity.
    bool context_may_profit(
        [[maybe_unused]] const PolicyResearchContext<T>& context,
        [[maybe_unused]] size_t input_coin,
        [[maybe_unused]] const T& spot, [[maybe_unused]] const T& external
    ) const {
#ifdef TWOCRYPTO_POLICY_HEADER
        if constexpr (compiled_detail::HasContextMayProfit<ChallengeFeePolicy<T>, T>::value) {
            if (kind == PolicyKind::Compiled) {
                return ChallengeFeePolicy<T>::context_may_profit(
                    compiled_state, params, config, context, input_coin, spot, external);
            }
        }
#endif
        return true;
    }

    // Effective report terms for coin_in in `context` (price_feed 0 keeps the stored one); nullopt without the hook.
    std::optional<PolicyReportTerms<T>> report_terms(
        [[maybe_unused]] const PolicyResearchContext<T>& context, [[maybe_unused]] std::size_t coin_in
    ) const {
#ifdef TWOCRYPTO_POLICY_HEADER
        if constexpr (compiled_detail::HasReportTerms<ChallengeFeePolicy<T>, T>::value) {
            if (kind == PolicyKind::Compiled)
                return ChallengeFeePolicy<T>::report_terms(compiled_state, params, config, context, coin_in);
        }
#endif
        return std::nullopt;
    }

    T get_fee([[maybe_unused]] const std::array<T, 2>& xp,
              [[maybe_unused]] const T& live_pool_D = T(0)) const {
        switch (kind) {
        case PolicyKind::None:
            return T(0);
        case PolicyKind::Compiled:
#ifdef TWOCRYPTO_POLICY_HEADER
            if constexpr (compiled_detail::HasLivePoolFee<ChallengeFeePolicy<T>, T>::value) {
                return ChallengeFeePolicy<T>::get_fee(
                    compiled_state, params, config, research, xp, live_pool_D
                );
            } else {
                return ChallengeFeePolicy<T>::get_fee(
                    compiled_state, params, config, research, xp
                );
            }
#else
            break;
#endif
        }
        throw std::logic_error("unsupported policy kind in fee hook");
    }

    // The caller must discard this view before changing policy state/context.
    auto prepare_fee([[maybe_unused]] const T& live_pool_D = T(0)) const {
#ifdef TWOCRYPTO_POLICY_HEADER
        if constexpr (compiled_detail::HasPreparedLivePoolFee<ChallengeFeePolicy<T>, T>::value) {
            auto prepared = ChallengeFeePolicy<T>::prepare_fee(
                compiled_state, params, config, research, live_pool_D);
            return [this, prepared, live_pool_D](const std::array<T, 2>& xp) {
                return kind == PolicyKind::Compiled ? prepared(xp) : get_fee(xp, live_pool_D);
            };
        } else if constexpr (compiled_detail::HasPreparedFee<ChallengeFeePolicy<T>, T>::value) {
            auto prepared = ChallengeFeePolicy<T>::prepare_fee(compiled_state, params, config, research);
            return [this, prepared, live_pool_D](const std::array<T, 2>& xp) {
                return kind == PolicyKind::Compiled ? prepared(xp) : get_fee(xp, live_pool_D);
            };
        } else
#endif
        {
            return [this, live_pool_D](const std::array<T, 2>& xp) { return get_fee(xp, live_pool_D); };
        }
    }

    // Share of the re-peg budget one price-scale step may spend (research); 0 leaves the native gate alone.
    T repeg_reserve() const {
#ifdef TWOCRYPTO_POLICY_HEADER
        if constexpr (compiled_detail::HasRepegReserve<ChallengeFeePolicy<T>, T>::value)
            if (kind == PolicyKind::Compiled) return ChallengeFeePolicy<T>::repeg_reserve(params);
#endif
        return T(0);
    }

    // LP floor the re-peg gate uses (research); the pool's protected floor unless the policy releases part of it.
    T repeg_floor(const T& floor) const {
#ifdef TWOCRYPTO_POLICY_HEADER
        if constexpr (compiled_detail::HasRepegFloor<ChallengeFeePolicy<T>, T>::value)
            if (kind == PolicyKind::Compiled) return ChallengeFeePolicy<T>::repeg_floor(compiled_state, params, floor);
#endif
        return floor;
    }

    // Smallest relative move a clipped re-peg step may make (research); 0: any.
    T repeg_min_progress() const {
#ifdef TWOCRYPTO_POLICY_HEADER
        if constexpr (compiled_detail::HasRepegMinProgress<ChallengeFeePolicy<T>, T>::value)
            if (kind == PolicyKind::Compiled) return ChallengeFeePolicy<T>::repeg_min_progress(params);
#endif
        return T(0);
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
        [[maybe_unused]] const std::array<T, 2>& xp,
        [[maybe_unused]] const T& price_scale,
        [[maybe_unused]] const T& price_oracle,
        [[maybe_unused]] const T& last_prices,
        [[maybe_unused]] const T& virtual_price,
        [[maybe_unused]] const T& xcp_profit,
        [[maybe_unused]] const T& d_value,
        [[maybe_unused]] uint64_t oracle_timestamp,
        [[maybe_unused]] const T& lp_floor = T(0),
        [[maybe_unused]] const T& vp_boosted = T(0)
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
            oracle_timestamp,
            lp_floor,
            vp_boosted
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
