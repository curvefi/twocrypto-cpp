// TwoCrypto pool (templated on numeric type)
// Mirrors contracts/twocrypto-ng/contracts/main/Twocrypto.vy (immutable reference).
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <chrono>
#include <cmath>
#include <type_traits>
#include <utility>
#include "policy.hpp"
#include "price_scale_actuator.hpp"
#include "stableswap_math.hpp"

namespace arb {
namespace pools {
namespace twocrypto_fx {

// Pool traits for numeric types. The primary template covers all floating-point
// pools; uint256 (the bit-parity path) has its own specialization. Exactly-
// representable constants use T(x); the two inexact ones (NOISE_FEE 1e-5,
// MINIMUM_LIQUIDITY 1e-14) keep their per-type decimal literal so each type
// parses the decimal directly and there is no double-rounding through a wider
// type.
template <typename T>
struct PoolTraits {
    static_assert(std::is_floating_point_v<T>, "PoolTraits primary template is floating-point only");
    static T PRECISION() { return T(1); }
    static T FEE_PRECISION() { return T(1); }
    static T NOISE_FEE() {
        if constexpr (std::is_same_v<T, float>) return 1e-5f;
        else if constexpr (std::is_same_v<T, long double>) return 1e-5L;
        else return 1e-5;
    }
    static T MINIMUM_LIQUIDITY() {
        if constexpr (std::is_same_v<T, float>) return 1e-14f;
        else if constexpr (std::is_same_v<T, long double>) return 1e-14L;
        else return 1e-14;
    }
    static T ZERO() { return T(0); }
    static T ONE() { return T(1); }
    static T ROUNDING_UNIT_XP() { return T(0); }
    static T max(const T& a, const T& b) { return a > b ? a : b; }
    static T min(const T& a, const T& b) { return a < b ? a : b; }
};

template <>
struct PoolTraits<uint256> {
    using T = uint256;
    static T PRECISION() { static T v("1000000000000000000"); return v; }
    static T FEE_PRECISION() { static T v("10000000000"); return v; }
    static T NOISE_FEE() { static T v("100000"); return v; }
    static T MINIMUM_LIQUIDITY() { return T(10000); }
    static T ZERO() { return T(0); }
    static T ONE() { return T(1); }
    static T ROUNDING_UNIT_XP() { return T(1); }
    static T max(const T& a, const T& b) { return a > b ? a : b; }
    static T min(const T& a, const T& b) { return a < b ? a : b; }
};

template <typename T>
class TwoCryptoPool {
public:
    using Ops = MathOps<T>;
    using Traits = PoolTraits<T>;
    static constexpr int N_COINS = 2;

    // State variables
    std::array<T, 2> balances{Traits::ZERO(), Traits::ZERO()};
    T D = Traits::ZERO();
    T totalSupply = Traits::ZERO();

    // Price variables
    T cached_price_scale = Traits::PRECISION();
    T cached_price_oracle = Traits::PRECISION();
    T last_prices = Traits::PRECISION();
    uint64_t last_timestamp = 0;

    // Floating-only EMA helper cache. The uint branch still computes wad_exp.
    uint64_t cached_ema_dt = 0;
    T cached_ema_alpha = Traits::ZERO();
    bool cached_ema_alpha_valid = false;

    // Parameters (normalized)
    T A = Traits::ZERO();
    T gamma = Traits::ZERO();
    T mid_fee = Traits::ZERO();
    T out_fee = Traits::ZERO();
    T fee_gamma = Traits::ZERO();
    T adjustment_step_min = Traits::ZERO();
    T adjustment_step_max = Traits::ZERO();
    T ma_time = Traits::ONE();
    T reserved_profit_fraction = Traits::FEE_PRECISION() / 2;
    T admin_fee = Traits::FEE_PRECISION() / 2;
    PolicyModel<T> policy{};

    // Profit tracking
    T xcp_profit = Traits::PRECISION();
    T lp_xcp_profit = Traits::PRECISION();
    T virtual_price = Traits::PRECISION();
    std::array<T, 2> admin_balances{Traits::ZERO(), Traits::ZERO()};
    uint64_t last_admin_fee_claim_timestamp = 0;

    // Token precisions
    std::array<T, 2> precisions{Traits::ONE(), Traits::ONE()};

    // Time for testing
    uint64_t block_timestamp = 0;

    // Donations
    T donation_shares = Traits::ZERO();
    T donation_shares_max_ratio = Traits::PRECISION() * 10 / 100; // 10%
    T donation_duration = T(7 * 86400);
    T last_donation_release_ts = Traits::ZERO();
    T donation_protection_expiry_ts = Traits::ZERO();
    T donation_protection_period = T(10 * 60);
    T donation_protection_lp_threshold = Traits::PRECISION() * 20 / 100; // 20%
    T donation_protection_extension_remainder = Traits::ZERO();
    // Value-semantic transaction boundary for all mutable pool state. The
    // policy snapshot preserves research context and only the active policy
    // state's small mutable fields; immutable config and inactive states stay
    // out of speculative rollback copies.
    struct MutableSnapshot {
        std::array<T, 2> balances{};
        T D{};
        T totalSupply{};

        T cached_price_scale{};
        T cached_price_oracle{};
        T last_prices{};
        uint64_t last_timestamp{0};
        uint64_t cached_ema_dt{0};
        T cached_ema_alpha{};
        bool cached_ema_alpha_valid{false};

        T xcp_profit{};
        T lp_xcp_profit{};
        T virtual_price{};
        std::array<T, 2> admin_balances{};
        uint64_t last_admin_fee_claim_timestamp{0};

        uint64_t block_timestamp{0};

        T donation_shares{};
        T last_donation_release_ts{};
        T donation_protection_expiry_ts{};
        T donation_protection_extension_remainder{};

        typename PolicyModel<T>::MutableSnapshot policy{};
    };

    MutableSnapshot mutable_snapshot() const {
        MutableSnapshot snapshot{};
        snapshot.balances = balances;
        snapshot.D = D;
        snapshot.totalSupply = totalSupply;
        snapshot.cached_price_scale = cached_price_scale;
        snapshot.cached_price_oracle = cached_price_oracle;
        snapshot.last_prices = last_prices;
        snapshot.last_timestamp = last_timestamp;
        snapshot.cached_ema_dt = cached_ema_dt;
        snapshot.cached_ema_alpha = cached_ema_alpha;
        snapshot.cached_ema_alpha_valid = cached_ema_alpha_valid;
        snapshot.xcp_profit = xcp_profit;
        snapshot.lp_xcp_profit = lp_xcp_profit;
        snapshot.virtual_price = virtual_price;
        snapshot.admin_balances = admin_balances;
        snapshot.last_admin_fee_claim_timestamp = last_admin_fee_claim_timestamp;
        snapshot.block_timestamp = block_timestamp;
        snapshot.donation_shares = donation_shares;
        snapshot.last_donation_release_ts = last_donation_release_ts;
        snapshot.donation_protection_expiry_ts = donation_protection_expiry_ts;
        snapshot.donation_protection_extension_remainder =
            donation_protection_extension_remainder;
        snapshot.policy = policy.mutable_snapshot();
        return snapshot;
    }

    void restore_mutable(const MutableSnapshot& snapshot) {
        balances = snapshot.balances;
        D = snapshot.D;
        totalSupply = snapshot.totalSupply;
        cached_price_scale = snapshot.cached_price_scale;
        cached_price_oracle = snapshot.cached_price_oracle;
        last_prices = snapshot.last_prices;
        last_timestamp = snapshot.last_timestamp;
        cached_ema_dt = snapshot.cached_ema_dt;
        cached_ema_alpha = snapshot.cached_ema_alpha;
        cached_ema_alpha_valid = snapshot.cached_ema_alpha_valid;
        xcp_profit = snapshot.xcp_profit;
        lp_xcp_profit = snapshot.lp_xcp_profit;
        virtual_price = snapshot.virtual_price;
        admin_balances = snapshot.admin_balances;
        last_admin_fee_claim_timestamp = snapshot.last_admin_fee_claim_timestamp;
        block_timestamp = snapshot.block_timestamp;
        donation_shares = snapshot.donation_shares;
        last_donation_release_ts = snapshot.last_donation_release_ts;
        donation_protection_expiry_ts = snapshot.donation_protection_expiry_ts;
        donation_protection_extension_remainder =
            snapshot.donation_protection_extension_remainder;
        policy.restore_mutable(snapshot.policy);
    }

private:
    void _claim_admin_fees() {
        // The product factory always has a non-zero fee receiver.  Token
        // transfers are outside the simulator's accounting boundary: the
        // balances were reduced when the fees accrued, so claiming only clears
        // the cached admin balances and advances the claim timestamp.
        constexpr uint64_t MIN_ADMIN_FEE_CLAIM_INTERVAL = 86400;
        if (block_timestamp < last_admin_fee_claim_timestamp ||
            block_timestamp - last_admin_fee_claim_timestamp <
                MIN_ADMIN_FEE_CLAIM_INTERVAL) {
            return;
        }
        if (admin_balances[0] == Traits::ZERO() &&
            admin_balances[1] == Traits::ZERO()) {
            return;
        }
        admin_balances = {Traits::ZERO(), Traits::ZERO()};
        last_admin_fee_claim_timestamp = block_timestamp;
    }

    template <typename Mutation>
    auto with_mutable_rollback(Mutation&& mutation)
        -> decltype(std::forward<Mutation>(mutation)()) {
        const MutableSnapshot before = mutable_snapshot();
        try {
            return std::forward<Mutation>(mutation)();
        } catch (...) {
            restore_mutable(before);
            throw;
        }
    }

public:
    bool uses_native_fee_model() const noexcept {
        return policy.kind == PolicyKind::None ||
            (policy.kind == PolicyKind::Compiled &&
             compiled_detail::uses_native_fee_v<T>);
    }


public:
    TwoCryptoPool(
        const std::array<T, 2>& _precisions,
        const T& _A,
        const T& _gamma,
        const T& _mid_fee,
        const T& _out_fee,
        const T& _fee_gamma,
        const T& _adjustment_step_min,
        const T& _adjustment_step_max,
        const T& _ma_time,
        const T& initial_price,
        const T& _reserved_profit_fraction = PoolTraits<T>::FEE_PRECISION() / 2,
        const T& _admin_fee = PoolTraits<T>::FEE_PRECISION() / 2,
        PolicyKind _policy_kind = PolicyKind::None,
        PolicyConfig<T> _policy_config = PolicyConfig<T>{}
    ) {
        precisions = _precisions;
        A = _A; gamma = _gamma;
        mid_fee = _mid_fee; out_fee = _out_fee; fee_gamma = _fee_gamma;
        adjustment_step_min = _adjustment_step_min;
        adjustment_step_max = _adjustment_step_max;
        ma_time = _ma_time;
        reserved_profit_fraction = _reserved_profit_fraction;
        admin_fee = _admin_fee;
        if (_policy_config.kind == PolicyKind::None) {
            _policy_config.kind = _policy_kind;
        }
        policy = PolicyModel<T>(_policy_config);
        policy.configure_pool(
            mid_fee,
            out_fee,
            fee_gamma,
            ma_time,
            Traits::PRECISION(),
            Traits::FEE_PRECISION(),
            A,
            gamma,
            adjustment_step_min,
            adjustment_step_max
        );

        cached_price_scale = initial_price;
        cached_price_oracle = initial_price;
        last_prices = initial_price;

        xcp_profit = Traits::PRECISION();
        lp_xcp_profit = Traits::PRECISION();
        virtual_price = Traits::PRECISION();

        block_timestamp = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        last_timestamp = block_timestamp;
    }

    T fee(const std::array<T, 2>& xp) const {
        return _fee(xp);
    }

    // Lower bound on the fee any trade can currently pay (any size, any
    // direction). Used by the arb sizing gate so fee structures that get
    // cheaper with size are not pre-filtered out. Errs low: never above
    // the true minimum.
    T fee_lower_bound() const {
        const T native_floor = Traits::min(mid_fee, out_fee);
        if (policy.kind == PolicyKind::None) {
            return _clamp_fee(native_floor);
        }
        return _clamp_fee(policy.fee_floor(native_floor));
    }

private:
    T _clamp_fee(const T& fee) const {
        const T min_fee = Traits::NOISE_FEE();
        const T max_fee = Traits::FEE_PRECISION();
        return Traits::min(max_fee, Traits::max(min_fee, fee));
    }

    // Mirrors vy _fee: B goes from PRECISION (balanced) to 0 (imbalanced),
    // fee interpolates mid_fee..out_fee. fee_gamma == 0 yields out_fee,
    // exactly as the reference formula does.
    T _native_fee(const std::array<T, 2>& xp) const {
        T B = xp[0] + xp[1];
        if (B == Traits::ZERO()) {
            return out_fee;  // formula limit; vy reverts on an empty pool
        }

        B = Traits::PRECISION() * N_COINS * N_COINS * xp[0] / B * xp[1] / B;
        B = fee_gamma * B /
            (fee_gamma * B / Traits::PRECISION() + Traits::PRECISION() - B);

        return (
            mid_fee * B + out_fee * (Traits::PRECISION() - B)
        ) / Traits::PRECISION();
    }

    // Mirrors vy _assert_balance: post-operation guard, tighter than the
    // 10_000:1 newton_D guard in math. Called on both tweak_price exits in
    // the reference; cpp checks it before committing state (vy asserts after
    // its writes but reverts atomically).
    void _assert_balance(const std::array<T, 2>& xp) const {
        if (!(xp[0] > Traits::ZERO() && xp[1] > Traits::ZERO() &&
              Traits::max(xp[0], xp[1]) / Traits::min(xp[0], xp[1]) < T(1000))) {
            throw std::runtime_error("!balance");
        }
    }

    // _xp: balances scaled to common precision with price_scale for coin1
    std::array<T, 2> _xp(
        const std::array<T, 2>& _balances,
        const T& price_scale
    ) const {
        return {
            _balances[0] * precisions[0],
            _balances[1] * precisions[1] * price_scale / Traits::PRECISION()
        };
    }

    // _fee: dynamic fee between mid_fee and out_fee based on balance skew.
    // A nonzero policy fee is the final answer, so avoid computing the native
    // fallback unless the policy explicitly returns zero. This changes neither
    // operation order nor arithmetic on either returned branch.
    T _fee(const std::array<T, 2>& xp) const {
        if (policy.kind != PolicyKind::None) {
            const T policy_fee = policy.get_fee(xp);
            if (policy_fee != Traits::ZERO()) {
                return _clamp_fee(policy_fee);
            }
            return _clamp_fee(_native_fee(xp));
        }
        return _clamp_fee(_native_fee(xp));
    }

    T _policy_price_target(uint64_t ts, const T& current_price_oracle) {
        if (policy.kind == PolicyKind::None) {
            return Traits::ZERO();
        }
        policy.prepare_price_scale_call(ts, current_price_oracle);
        return policy.get_price_scale();
    }

    void _update_policy_state(
        const std::array<T, 2>& xp,
        const T& price_scale,
        const T& price_oracle,
        const T& last_prices,
        const T& virtual_price,
        const T& xcp_profit,
        const T& D
    ) {
        if (policy.kind == PolicyKind::None) {
            return;
        }
        policy.update_state(
            xp,
            price_scale,
            price_oracle,
            last_prices,
            virtual_price,
            xcp_profit,
            D,
            last_timestamp
        );
    }

    T _apply_admin_d_token_fee(
        std::array<T, 2>& local_balances,
        const T& d_token_fee,
        const T& fee_supply
    ) {
        T admin_d_token_fee = (
            d_token_fee * reserved_profit_fraction * admin_fee
        ) / (Traits::FEE_PRECISION() * Traits::FEE_PRECISION());
        if (admin_d_token_fee > Traits::ZERO() && fee_supply > Traits::ZERO()) {
            for (size_t i = 0; i < N_COINS; ++i) {
                T admin_amount = local_balances[i] * admin_d_token_fee / fee_supply;
                admin_balances[i] += admin_amount;
                local_balances[i] -= admin_amount;
            }
        }
        return admin_d_token_fee;
    }

    // _xcp: cross-product invariant in xcp units
    T _xcp(const T& _D, const T& price_scale) const {
        if constexpr (std::is_same_v<T, uint256>) {
            auto sqrt_price = boost::multiprecision::sqrt(
                Traits::PRECISION() * price_scale
            );
            return _D * Traits::PRECISION() / N_COINS / sqrt_price;
        } else if constexpr (std::is_same_v<T, long double>) {
            long double sp = std::sqrt(static_cast<long double>(Traits::PRECISION() * price_scale));
            return _D * Traits::PRECISION() / T(N_COINS) / T(sp);
        } else {
            double sp = std::sqrt(static_cast<double>(Traits::PRECISION() * price_scale));
            return _D * Traits::PRECISION() / T(N_COINS) / T(sp);
        }
    }

    // _donation_shares: unlocked donation supply (optionally protected)
    T _donation_shares(bool donation_protection = true) const {
        if (donation_shares == Traits::ZERO()) {
            return Traits::ZERO();
        }

        T elapsed  = T(block_timestamp) - last_donation_release_ts;
        T unlocked = donation_shares * elapsed / donation_duration;
        if (unlocked > donation_shares) {
            unlocked = donation_shares;
        }

        if (!donation_protection) {
            return unlocked;
        }

        T protection_factor = Traits::ZERO();
        if (donation_protection_expiry_ts > T(block_timestamp)) {
            protection_factor = (
                (donation_protection_expiry_ts - T(block_timestamp)) *
                Traits::PRECISION() /
                donation_protection_period
            );
            if (protection_factor > Traits::PRECISION()) {
                protection_factor = Traits::PRECISION();
            }
        }

        return (
            unlocked * (Traits::PRECISION() - protection_factor)
        ) / Traits::PRECISION();
    }

    // _calc_token_fee: liquidity op fee approximation
    T _calc_token_fee(
        const std::array<T, 2>& amounts,
        const std::array<T, 2>& xp,
        bool donation,
        bool deposit,
        bool withdrawal = false
    ) const {
        if (donation) {
            return Traits::NOISE_FEE();
        }

        std::array<T, 2> balance_basis = balances;
        if (withdrawal) {
            if (balance_basis[0] < amounts[0] || balance_basis[1] < amounts[1]) {
                throw std::runtime_error("withdrawal fee balance basis underflow");
            }
            balance_basis[0] -= amounts[0];
            balance_basis[1] -= amounts[1];
        }

        T denom = balance_basis[1] * precisions[1];
        T balances_ratio = Traits::ZERO();
        if (denom > Traits::ZERO()) {
            balances_ratio = (
                balance_basis[0] * precisions[0] * Traits::PRECISION()
            ) / denom;
        }

        std::array<T, 2> amounts_scaled = {
            amounts[0] * precisions[0],
            amounts[1] * precisions[1] * balances_ratio / Traits::PRECISION()
        };

        T fee_prime = _fee(xp) * N_COINS / (4 * (N_COINS - 1));

        T S = amounts_scaled[0] + amounts_scaled[1];
        if (S == Traits::ZERO()) {
            return Traits::NOISE_FEE();
        }

        T avg   = S / N_COINS;
        T diff0 = (amounts_scaled[0] > avg) ? (amounts_scaled[0] - avg)
                                            : (avg - amounts_scaled[0]);
        T diff1 = (amounts_scaled[1] > avg) ? (amounts_scaled[1] - avg)
                                            : (avg - amounts_scaled[1]);
        T Sdiff = diff0 + diff1;

        T lp_spam_penalty_fee = Traits::ZERO();
        if (deposit && donation_protection_expiry_ts > T(block_timestamp)) {
            T protection_factor = Traits::min(
                (donation_protection_expiry_ts - T(block_timestamp)) *
                    Traits::PRECISION() / donation_protection_period,
                Traits::PRECISION()
            );
            // vy floors once over the whole product chain; an intermediate
            // floor on donation_shares/totalSupply gives off-by-one fees.
            lp_spam_penalty_fee = Traits::min(
                fee_prime,
                protection_factor * fee_prime * donation_shares
                    / totalSupply / donation_shares_max_ratio
            );
        }

        return fee_prime * Sdiff / S + Traits::NOISE_FEE() + lp_spam_penalty_fee;
    }

public:
    // Cheap tick to update EMA/oracle and possibly adjust price_scale without a swap
    void tick() {
        auto A_gamma = std::array<T, 2>{ A, gamma };
        const auto xp = _xp(balances, cached_price_scale);
        cached_price_scale = tweak_price(A_gamma, xp, D, virtual_price);
    }

    // Native contract-side oracle projected to `now` without mutating its
    // timestamp/cache. lp_price_at uses this read-only projection; tweak_price
    // performs the same EMA update on a real call, including the fixed tick.
    T projected_price_oracle_at(uint64_t now) const {
        T projected = cached_price_oracle;
        if (last_timestamp >= now) return projected;

        const uint64_t dt_raw = now - last_timestamp;
        const T dt = T(dt_raw);
        T capped = last_prices;
        const T lower = cached_price_scale / 2;
        if (capped < lower) capped = lower;
        if (capped > 2 * cached_price_scale) capped = 2 * cached_price_scale;

        if constexpr (std::is_same_v<T, uint256>) {
            const auto neg = int256(
                -(
                    int256(dt) *
                    int256(PoolTraits<T>::PRECISION()) /
                    int256(ma_time)
                )
            );
            const T alpha = MathOps<T>::wad_exp(neg);
            projected = (
                capped * (PoolTraits<T>::PRECISION() - alpha) +
                projected * alpha
            ) / PoolTraits<T>::PRECISION();
        } else {
            using std::exp;
            const T alpha =
                cached_ema_alpha_valid && cached_ema_dt == dt_raw
                    ? cached_ema_alpha
                    : exp(-dt / ma_time);
            projected = capped * (T(1) - alpha) + projected * alpha;
        }
        return projected;
    }

    // add_liquidity: deposit into the pool; supports donation mode with cap semantics
    T add_liquidity(
        const std::array<T, 2>& amounts,
        T min_mint_amount,
        bool donation = false,
        T* charged_lp_fee = nullptr
    ) {
        const auto minted = add_liquidity_impl(
            amounts, min_mint_amount, donation, charged_lp_fee, nullptr
        );
        // A null result is possible only for the explicit floating-point
        // try-add surface below. The public/Vyper-shaped method keeps its
        // historical throwing behavior for every rejection.
        return *minted;
    }

    std::optional<T> try_add_donation(
        const std::array<T, 2>& amounts,
        T min_mint_amount,
        std::string& rejection
    ) {
        static_assert(
            std::is_floating_point_v<T>,
            "throwless donation preview is floating-only"
        );
        return add_liquidity_impl(
            amounts, min_mint_amount, true, nullptr, &rejection
        );
    }

private:
    std::optional<T> add_liquidity_impl(
        const std::array<T, 2>& amounts,
        T min_mint_amount,
        bool donation,
        T* charged_lp_fee,
        std::string* rejection_out
    ) {
        const MutableSnapshot before = mutable_snapshot();
        T local_charged_lp_fee = Traits::ZERO();
        try {
            auto result = add_liquidity_unchecked(
                amounts,
                min_mint_amount,
                donation,
                charged_lp_fee != nullptr ? &local_charged_lp_fee : nullptr,
                rejection_out
            );
            if (!result.has_value()) {
                restore_mutable(before);
                return std::nullopt;
            }
            if (charged_lp_fee != nullptr) {
                *charged_lp_fee = local_charged_lp_fee;
            }
            return result;
        } catch (...) {
            restore_mutable(before);
            throw;
        }
    }

    std::optional<T> add_liquidity_unchecked(
        const std::array<T, 2>& amounts,
        T min_mint_amount,
        bool donation,
        T* charged_lp_fee,
        std::string* rejection_out
    ) {
        if (amounts[0] + amounts[1] == Traits::ZERO()) {
            throw std::invalid_argument("no coins to add");
        }
        if (donation && D == Traits::ZERO()) {
            throw std::runtime_error("donation not allowed on empty pool");
        }
        const auto reject = [&](const char* reason)
            -> std::optional<T> {
            if constexpr (std::is_floating_point_v<T>) {
                if (rejection_out != nullptr) {
                    *rejection_out = reason;
                    return std::nullopt;
                }
            }
            throw std::runtime_error(reason);
        };

        auto new_balances = std::array<T, 2>{
            balances[0] + amounts[0],
            balances[1] + amounts[1]
        };

        T price_scale = cached_price_scale;
        auto xp = _xp(new_balances, price_scale);

        auto A_gamma = std::array<T, 2>{ A, gamma };

        T old_D = D;
        T D_new = Ops::newton_D(A_gamma[0], A_gamma[1], xp, old_D);
        T token_supply = totalSupply;
        T vp_preop = virtual_price;
        if (old_D > Traits::ZERO() && token_supply > Traits::ZERO()) {
            vp_preop = Traits::PRECISION() * _xcp(old_D, price_scale) / token_supply;
        }

        T d_token = Traits::ZERO();
        if (old_D > Traits::ZERO()) {
            d_token = token_supply * D_new / old_D - token_supply;
        } else {
            d_token = _xcp(D_new, price_scale);
        }
        // vy: assert d_token > 0, "nothing minted". Without this a dust
        // deposit makes the fee subtraction below wrap on uint256.
        if (!(d_token > Traits::ZERO())) {
            return reject("nothing minted");
        }

        if (old_D > Traits::ZERO()) {
            T approx_fee  = _calc_token_fee(amounts, xp, donation, /*deposit=*/true);
            T d_token_fee = approx_fee * d_token / PoolTraits<T>::FEE_PRECISION();
            if constexpr (std::is_same_v<T, uint256>) {
                d_token_fee += Traits::ONE();
            }
            if (charged_lp_fee != nullptr) {
                *charged_lp_fee = d_token_fee;
            }
            d_token -= d_token_fee;
            if (!donation && d_token_fee > Traits::ZERO() &&
                reserved_profit_fraction > Traits::ZERO() && admin_fee > Traits::ZERO()) {
                T fee_supply = token_supply + d_token + d_token_fee;
                _apply_admin_d_token_fee(new_balances, d_token_fee, fee_supply);
                xp = _xp(new_balances, price_scale);
                D_new = Ops::newton_D(A_gamma[0], A_gamma[1], xp, D_new);
            }
        }

        // Constraints before commit (vy checks min_mint last but reverts
        // atomically; cpp must reject before mutating any state)
        if (old_D > Traits::ZERO()) {
            if (donation) {
                T new_donation_shares = donation_shares + d_token;
                T ratio = (
                    new_donation_shares * Traits::PRECISION()
                ) / (token_supply + d_token);
                if (ratio > donation_shares_max_ratio) {
                    return reject("donation above cap");
                }
            }
            if (d_token < min_mint_amount) {
                return reject("slippage");
            }
        } else {
            if (!(d_token > Traits::MINIMUM_LIQUIDITY())) {
                return reject("initial liquidity too low");
            }
            if (d_token - Traits::MINIMUM_LIQUIDITY() < min_mint_amount) {
                return reject("slippage");
            }
        }

        // Commit
        balances = new_balances;
        if (old_D > Traits::ZERO()) {
            D = D_new;

            if (donation) {
                T new_donation_shares = donation_shares + d_token;
                T unlocked = _donation_shares(false);
                T new_elapsed = Traits::ZERO();
                if (new_donation_shares > Traits::ZERO()) {
                    new_elapsed = (unlocked * donation_duration) / new_donation_shares;
                }
                last_donation_release_ts = T(block_timestamp) - new_elapsed;
                donation_shares = new_donation_shares;
                totalSupply += d_token;
            } else {
                T relative_lp_add = (
                    d_token * Traits::PRECISION()
                ) / (token_supply + d_token);

                if (relative_lp_add > Traits::ZERO() && donation_shares > Traits::ZERO()) {
                    T raw_extension = (
                        relative_lp_add * donation_protection_period
                    ) + donation_protection_extension_remainder;
                    T extension_seconds = raw_extension / donation_protection_lp_threshold;

                    T current_expiry = (
                        donation_protection_expiry_ts > T(block_timestamp)
                    ) ? donation_protection_expiry_ts : T(block_timestamp);

                    T uncapped_expiry = current_expiry + extension_seconds;
                    T max_expiry = T(block_timestamp) + donation_protection_period;

                    if (uncapped_expiry >= max_expiry) {
                        donation_protection_expiry_ts = max_expiry;
                        donation_protection_extension_remainder = Traits::ZERO();
                    } else {
                        donation_protection_expiry_ts = uncapped_expiry;
                        if constexpr (std::is_same_v<T, uint256>) {
                            donation_protection_extension_remainder = raw_extension % donation_protection_lp_threshold;
                        } else {
                            donation_protection_extension_remainder = std::fmod(
                                raw_extension,
                                donation_protection_lp_threshold
                            );
                        }
                    }
                }
                totalSupply += d_token;
            }

            cached_price_scale = tweak_price(A_gamma, xp, D_new, vp_preop);
        } else {
            D = D_new;
            virtual_price = Traits::PRECISION();
            xcp_profit    = Traits::PRECISION();
            lp_xcp_profit = Traits::PRECISION();
            // vy mints MINIMUM_LIQUIDITY to the pool itself: dead shares that
            // stay in totalSupply forever and are not returned to the seeder.
            totalSupply  += d_token;
            d_token      -= Traits::MINIMUM_LIQUIDITY();
            _update_policy_state(xp, price_scale, cached_price_oracle, last_prices, virtual_price, xcp_profit, D);
        }
        return d_token;
    }

public:

    // Exact read-only mirrors of TwocryptoView.get_dy/get_dx for the current
    // non-ramping simulator state.
    T get_dy(size_t idx_i, size_t idx_j, const T& dx) const {
        if (idx_i == idx_j || idx_i >= N_COINS || idx_j >= N_COINS) {
            throw std::invalid_argument("coin index out of range");
        }
        if (!(dx > Traits::ZERO())) {
            throw std::invalid_argument("do not exchange 0 coins");
        }

        const T price_scale = cached_price_scale;
        std::array<T, 2> raw_balances = balances;
        raw_balances[idx_i] += dx;
        auto xp = _xp(raw_balances, price_scale);
        const T y = Ops::get_y_unchecked(A, gamma, xp, D, idx_j);
        if (y >= xp[idx_j]) {
            throw std::runtime_error("unsafe value for y");
        }
        T dy = xp[idx_j] - y - Traits::ROUNDING_UNIT_XP();
        xp[idx_j] = y;
        if (idx_j > 0) {
            dy = dy * Traits::PRECISION() / price_scale;
        }
        dy /= precisions[idx_j];
        return dy - _fee(xp) * dy / Traits::FEE_PRECISION();
    }

    T get_dx(size_t idx_i, size_t idx_j, const T& dy, size_t n_iter = 5) const {
        if (idx_i == idx_j || idx_i >= N_COINS || idx_j >= N_COINS) {
            throw std::invalid_argument("coin index out of range");
        }
        if (!(dy > Traits::ZERO())) {
            throw std::invalid_argument("do not exchange out 0 coins");
        }
        if (n_iter > 100) {
            throw std::invalid_argument("get_dx iteration bound exceeded");
        }

        const T price_scale = cached_price_scale;
        T dy_with_fee = dy;
        T dx = Traits::ZERO();
        for (size_t iteration = 0; iteration < n_iter; ++iteration) {
            std::array<T, 2> raw_balances = balances;
            if (raw_balances[idx_j] < dy_with_fee) {
                throw std::runtime_error("insufficient output balance");
            }
            raw_balances[idx_j] -= dy_with_fee;
            auto xp = _xp(raw_balances, price_scale);
            const T x = Ops::get_y_unchecked(A, gamma, xp, D, idx_i);
            if (x < xp[idx_i]) {
                throw std::runtime_error("unsafe value for x");
            }
            dx = x - xp[idx_i];
            xp[idx_i] = x;
            if (idx_i > 0) {
                dx = dx * Traits::PRECISION() / price_scale;
            }
            dx /= precisions[idx_i];
            const T fee_dy = _fee(xp) * dy_with_fee / Traits::FEE_PRECISION();
            dy_with_fee = dy + fee_dy + Traits::ONE();
        }
        return dx;
    }

    // remove_liquidity: burn LP to withdraw proportionally (no fees, no
    // tweak_price - mirrors vy's math-free safe withdrawal)
    T single_caller_lp_balance() const {
        if (totalSupply < donation_shares) return Traits::ZERO();
        const T issued_to_accounts = totalSupply - donation_shares;
        if (issued_to_accounts < Traits::MINIMUM_LIQUIDITY()) {
            return Traits::ZERO();
        }
        return issued_to_accounts - Traits::MINIMUM_LIQUIDITY();
    }

    std::array<T, 2> remove_liquidity(
        T amount,
        const std::array<T, 2>& min_amounts
    ) {
        // The parity adapter has one persistent caller. Donation shares and
        // MINIMUM_LIQUIDITY belong to the pool, not that caller.
        if (amount > single_caller_lp_balance()) {
            throw std::invalid_argument("insufficient LP tokens");
        }

        const T total_supply = totalSupply;
        std::array<T, 2> withdraw_amounts{};
        for (size_t i = 0; i < N_COINS; ++i) {
            // Withdraws slightly less -> favors LPs already
            withdraw_amounts[i] = balances[i] * amount / total_supply;
            if (withdraw_amounts[i] < min_amounts[i]) {
                throw std::runtime_error("slippage");
            }
        }

        std::optional<typename PolicyModel<T>::MutableSnapshot>
            policy_before_update;
        if (amount > Traits::ZERO() && policy.kind != PolicyKind::None) {
            policy_before_update.emplace(policy.mutable_snapshot());
        }

        // All checks passed: commit (vy reverts atomically; cpp mutates last)
        totalSupply -= amount;
        // Reduce D proportionally to the amount of tokens leaving.
        D = D - (D * amount / total_supply);
        for (size_t i = 0; i < N_COINS; ++i) {
            balances[i] -= withdraw_amounts[i];
        }

        if (policy_before_update) {
            T price_scale = cached_price_scale;
            const std::array<T, 2> xp = _xp(balances, price_scale);
            try {
                _update_policy_state(
                    xp,
                    price_scale,
                    cached_price_oracle,
                    last_prices,
                    virtual_price,
                    xcp_profit,
                    D
                );
            } catch (...) {
                // Vyper makes this external policy call best-effort. Its
                // failed call reverts policy state, but not the withdrawal.
                policy.restore_mutable(*policy_before_update);
            }
        }

        return withdraw_amounts;
    }

    // Fixed-out withdrawal: burn an exact LP amount while fixing the amount
    // withdrawn from coin i. Mirrors the pinned Vyper
    // _calc_withdraw_fixed_out/_remove_liquidity_fixed_out path.
    T remove_liquidity_fixed_out(
        T token_amount,
        size_t idx_i,
        T amount_i,
        T min_amount_j,
        T* charged_lp_fee = nullptr
    ) {
        return with_mutable_rollback([&]() -> T {
            _claim_admin_fees();
            if (idx_i >= N_COINS) {
                throw std::invalid_argument("coin index out of range");
            }
            if (token_amount > single_caller_lp_balance()) {
                throw std::runtime_error("!amount");
            }
            const size_t idx_j = 1 - idx_i;
            const T token_supply = totalSupply;
            const T price_scale = cached_price_scale;
            const auto A_gamma = std::array<T, 2>{A, gamma};
            const auto xp = _xp(balances, price_scale);
            const T D_preop = D;  // no A/gamma ramping in the simulator

            T dD = token_amount * D_preop / token_supply;
            auto xp_new = xp;
            const std::array<T, 2> price_scales{
                Traits::PRECISION() * precisions[0],
                price_scale * precisions[1],
            };

            std::array<T, 2> amountsp{Traits::ZERO(), Traits::ZERO()};
            amountsp[idx_i] = (
                amount_i * price_scales[idx_i] + Traits::PRECISION() - Traits::ONE()
            ) / Traits::PRECISION();
            if (xp_new[idx_i] < amountsp[idx_i] || D_preop < dD) {
                throw std::runtime_error("fixed-out constrained xp underflow");
            }
            xp_new[idx_i] -= amountsp[idx_i];

            T y = Ops::get_y_unchecked(
                A_gamma[0], A_gamma[1], xp_new, D_preop - dD, idx_j
            ) + Traits::ROUNDING_UNIT_XP();
            if (xp[idx_j] < y) {
                throw std::runtime_error("fixed-out preliminary y underflow");
            }
            amountsp[idx_j] = xp[idx_j] - y;
            xp_new[idx_j] = y;

            std::array<T, 2> amounts{Traits::ZERO(), Traits::ZERO()};
            amounts[idx_i] = amount_i;
            if (idx_i == 0) {
                amounts[1] = amountsp[1] * Traits::PRECISION()
                    / precisions[1] / price_scale;
            } else {
                amounts[0] = amountsp[0] / precisions[0];
            }
            if (amounts[0] + amounts[1] == Traits::ZERO()) {
                throw std::runtime_error("!tokens");
            }

            const T approx_fee = _calc_token_fee(
                amounts, xp_new, /*donation=*/false,
                /*deposit=*/false, /*withdrawal=*/true
            );
            const T dD_fee = dD * approx_fee / Traits::FEE_PRECISION()
                + Traits::ROUNDING_UNIT_XP();
            if (dD < dD_fee) {
                throw std::runtime_error("fee exceeds invariant burn");
            }
            dD -= dD_fee;

            y = Ops::get_y_unchecked(
                A_gamma[0], A_gamma[1], xp_new, D_preop - dD, idx_j
            ) + Traits::ROUNDING_UNIT_XP();
            if (xp[idx_j] < y) {
                throw std::runtime_error("fixed-out final y underflow");
            }
            const T dy = (xp[idx_j] - y) * Traits::PRECISION()
                / price_scales[idx_j];
            xp_new[idx_j] = y;
            const T D_new = D_preop - dD;
            if (dy < min_amount_j) {
                throw std::runtime_error("slippage");
            }

            const T vp_preop = Traits::PRECISION()
                * _xcp(D_preop, price_scale) / token_supply;
            const T d_token_fee = approx_fee * token_amount
                / Traits::FEE_PRECISION() + Traits::ROUNDING_UNIT_XP();

            auto local_balances = balances;
            if (local_balances[idx_i] < amount_i || local_balances[idx_j] < dy) {
                throw std::runtime_error("fixed-out transfer balance underflow");
            }
            local_balances[idx_i] -= amount_i;
            local_balances[idx_j] -= dy;

            auto xp_commit = xp_new;
            T D_commit = D_new;
            if (
                d_token_fee > Traits::ZERO() &&
                reserved_profit_fraction > Traits::ZERO() &&
                admin_fee > Traits::ZERO()
            ) {
                const T fee_supply = token_supply - token_amount + d_token_fee;
                _apply_admin_d_token_fee(local_balances, d_token_fee, fee_supply);
                xp_commit = _xp(local_balances, price_scale);
                D_commit = Ops::newton_D(
                    A_gamma[0], A_gamma[1], xp_commit, Traits::ZERO()
                );
            }

            totalSupply -= token_amount;
            cached_price_scale = tweak_price(
                A_gamma, xp_commit, D_commit, vp_preop
            );
            balances = local_balances;
            if (charged_lp_fee != nullptr) *charged_lp_fee = d_token_fee;
            return dy;
        });
    }

    // exchange: swap coin i for coin j
    std::array<T, 3> exchange(
        T i,
        T j,
        T dx,
        T min_dy
    ) {
        return with_mutable_rollback([&]() -> std::array<T, 3> {
        size_t idx_i = static_cast<size_t>(i);
        size_t idx_j = static_cast<size_t>(j);

        if (idx_i == idx_j || idx_i >= N_COINS || idx_j >= N_COINS) {
            throw std::invalid_argument("coin index out of range");
        }
        if (dx == Traits::ZERO()) {
            throw std::invalid_argument("zero dx");
        }

        T price_scale = cached_price_scale;
        T vp_preop = (totalSupply > Traits::ZERO())
            ? (Traits::PRECISION() * _xcp(D, price_scale) / totalSupply)
            : virtual_price;

        const T balance0 = balances[0] + (idx_i == 0 ? dx : Traits::ZERO());
        const T balance1 = balances[1] + (idx_i == 1 ? dx : Traits::ZERO());
        std::array<T, 2> xp{
            balance0 * precisions[0],
            balance1 * precisions[1] * price_scale / Traits::PRECISION()
        };

        auto A_gamma = std::array<T, 2>{ A, gamma };

        const T y_out = Ops::get_y_unchecked(A_gamma[0], A_gamma[1], xp, D, idx_j);
        // Checked-subtraction semantics of the reference: get_y converges to
        // +/-1, so a dust trade can round to y >= xp[j]; vy reverts there
        // (at "xp[j] - y_out[0]" or "dy -= 1") instead of wrapping.
        if (y_out >= xp[idx_j]) {
            throw std::runtime_error("zero dy");
        }
        T dy = xp[idx_j] - y_out;
        xp[idx_j] -= dy;
        dy -= PoolTraits<T>::ROUNDING_UNIT_XP();

        if (idx_j > 0) {
            dy = dy * Traits::PRECISION() / price_scale;
        }
        dy = dy / precisions[idx_j];

        T fee = _fee(xp) * dy / PoolTraits<T>::FEE_PRECISION();
        dy -= fee;
        if (dy < min_dy) {
            throw std::runtime_error("slippage");
        }

        balances[idx_i] += dx;
        balances[idx_j] -= dy;
        auto xp_new = _xp(balances, price_scale);
        T D_new = Ops::newton_D(A_gamma[0], A_gamma[1], xp_new, D);
        D = D_new;
        T admin_fee_amount = (
            fee * reserved_profit_fraction * admin_fee
        ) / (Traits::FEE_PRECISION() * Traits::FEE_PRECISION());
        if (admin_fee_amount > Traits::ZERO()) {
            admin_balances[idx_j] += admin_fee_amount;
            balances[idx_j] -= admin_fee_amount;
            xp_new = _xp(balances, price_scale);
            D_new = Ops::newton_D(A_gamma[0], A_gamma[1], xp_new, D_new);
            D = D_new;
        }
        T new_price_scale = tweak_price(A_gamma, xp_new, D_new, vp_preop);
        return { dy, fee, new_price_scale };
        });
    }

    // Floating arb fast path: commit an exchange whose winning preview was
    // computed against the current pool state by simulate_exchange_once.
    std::array<T, 3> exchange_from_preview(
        size_t idx_i,
        size_t idx_j,
        T dx,
        T dy_after_fee,
        T fee
    ) {
        return with_mutable_rollback([&]() -> std::array<T, 3> {
        static_assert(std::is_floating_point_v<T>, "exchange_from_preview is floating-only");

        T price_scale = cached_price_scale;
        T vp_preop = (totalSupply > Traits::ZERO())
            ? (Traits::PRECISION() * _xcp(D, price_scale) / totalSupply)
            : virtual_price;

        auto A_gamma = std::array<T, 2>{ A, gamma };

        balances[idx_i] += dx;
        balances[idx_j] -= dy_after_fee;
        auto xp_new = _xp(balances, price_scale);
        T D_new = Ops::newton_D(A_gamma[0], A_gamma[1], xp_new, D);
        D = D_new;
        T admin_fee_amount = (
            fee * reserved_profit_fraction * admin_fee
        ) / (Traits::FEE_PRECISION() * Traits::FEE_PRECISION());
        if (admin_fee_amount > Traits::ZERO()) {
            admin_balances[idx_j] += admin_fee_amount;
            balances[idx_j] -= admin_fee_amount;
            xp_new = _xp(balances, price_scale);
            D_new = Ops::newton_D(A_gamma[0], A_gamma[1], xp_new, D_new);
            D = D_new;
        }
        T new_price_scale = tweak_price(A_gamma, xp_new, D_new, vp_preop);
        return { dy_after_fee, fee, new_price_scale };
        });
    }

    // Mirrors vy tweak_price(A_gamma, _xp, D, vp_preop). Locals that shadow
    // storage in vy (virtual_price, donation_shares) are named vp and
    // donation_unlocked here to avoid colliding with the members.
    T tweak_price(
        const std::array<T, 2>& _A_gamma,
        const std::array<T, 2>& xp,
        T _D,
        T vp_preop
    ) {
        T price_oracle = cached_price_oracle;
        T price_scale  = cached_price_scale;

        // EMA update
        uint64_t last_ts = last_timestamp;
        if (last_ts < block_timestamp) {
            uint64_t dt_raw = block_timestamp - last_ts;
            T dt = T(dt_raw);
            if constexpr (std::is_same_v<T, uint256>) {
                auto neg = int256(
                    -(
                        int256(dt) *
                        int256(PoolTraits<T>::PRECISION()) /
                        int256(ma_time)
                    )
                );
                T alpha  = MathOps<T>::wad_exp(neg);
                T capped = last_prices;
                T lower = price_scale / 2;
                if (capped < lower) capped = lower;
                if (capped > 2 * price_scale) capped = 2 * price_scale;
                price_oracle = (
                    capped * (PoolTraits<T>::PRECISION() - alpha) + price_oracle * alpha
                ) / PoolTraits<T>::PRECISION();
            } else {
                T alpha;
                if (cached_ema_alpha_valid && cached_ema_dt == dt_raw) {
                    alpha = cached_ema_alpha;
                } else {
                    using std::exp;
                    alpha = exp(-dt / ma_time);
                    cached_ema_dt = dt_raw;
                    cached_ema_alpha = alpha;
                    cached_ema_alpha_valid = true;
                }
                T capped = last_prices;
                T lower = price_scale / 2;
                if (capped < lower) capped = lower;
                if (capped > 2 * price_scale) capped = 2 * price_scale;

                price_oracle = capped * (T(1) - alpha) + price_oracle * alpha;
            }
            cached_price_oracle = price_oracle;
            last_timestamp      = block_timestamp;
        }

        // Update last_prices from current state
        last_prices = (
            MathOps<T>::get_p(xp, _D, _A_gamma) * price_scale
        ) / PoolTraits<T>::PRECISION();

        // Compute current virtual price and profits
        T total_supply      = totalSupply;
        T donation_unlocked = _donation_shares();
        T locked_supply     = total_supply - donation_unlocked;

        T old_virtual_price = virtual_price;
        T xcp               = _xcp(_D, price_scale);
        T vp = (total_supply > PoolTraits<T>::ZERO())
            ? (PoolTraits<T>::PRECISION() * xcp / total_supply)
            : PoolTraits<T>::PRECISION();

        // vy: assert vp >= vp_preop and (is_ramping or vp >= old_virtual_price).
        // No ramping here, so the cached-VP leg is unconditional on the parity
        // path; floats keep only the vp_preop check (ulp noise on the cached
        // baseline would spuriously throw).
        if (vp < vp_preop) {
            throw std::runtime_error("virtual price decreased");
        }
        if constexpr (std::is_same_v<T, uint256>) {
            if (vp < old_virtual_price) {
                throw std::runtime_error("virtual price decreased");
            }
        }

        T old_xcp_profit = xcp_profit;
        if (vp > old_virtual_price) {
            xcp_profit = xcp_profit + vp - old_virtual_price;
            if (xcp_profit > Traits::PRECISION()) {
                T baseline = Traits::max(old_xcp_profit, Traits::PRECISION());
                T d_profit = (xcp_profit > baseline) ? (xcp_profit - baseline) : Traits::ZERO();
                T denom = (
                    Traits::FEE_PRECISION() * Traits::FEE_PRECISION() -
                    reserved_profit_fraction * admin_fee
                );
                if (d_profit > Traits::ZERO() && denom > Traits::ZERO()) {
                    lp_xcp_profit += (
                        d_profit * reserved_profit_fraction * (Traits::FEE_PRECISION() - admin_fee)
                    ) / denom;
                }
            }
        } else {
            // With the non-ramping VP assert above, the uint path reaches
            // this branch only at vp == old_virtual_price (delta 0); floats
            // absorb representation noise here (clamped, never underflows).
            T vp_delta = old_virtual_price - vp;
            xcp_profit = (xcp_profit > vp_delta) ? (xcp_profit - vp_delta) : Traits::ZERO();
            if (lp_xcp_profit > Traits::PRECISION() && vp_delta <= (lp_xcp_profit - Traits::PRECISION())) {
                lp_xcp_profit -= vp_delta;
            } else {
                lp_xcp_profit = Traits::PRECISION();
            }
        }
        // By not counting unlocked donation shares, virtual_price is boosted,
        // leading to a rebalance trigger (approximate readiness condition).
        T vp_boosted = (locked_supply > PoolTraits<T>::ZERO())
            ? (PoolTraits<T>::PRECISION() * xcp / locked_supply)
            : vp;
        if (vp_boosted < vp) {
            throw std::runtime_error("negative donation");
        }

        // Rebalance liquidity if there's enough profit (once per block)
        const T lp_repeg_floor = lp_xcp_profit;
        const bool target_profit_ready = vp_boosted > lp_repeg_floor;
        const bool target_block_ready = last_ts < block_timestamp;
        if (target_profit_ready && target_block_ready) {
            const T p_policy = _policy_price_target(block_timestamp, price_oracle);
            const auto actuator_preview = preview_price_scale_actuator(
                p_policy,
                price_oracle,
                price_scale,
                PoolTraits<T>::PRECISION(),
                adjustment_step_min,
                adjustment_step_max
            );
            // vy: p_new defaults to price_scale and the rebalance body runs
            // only if the step-limited move actually changes it (the integer
            // floor can round the move back to price_scale).
            const T p_new = actuator_preview.p_new;

            if (p_new != price_scale) {
                auto xp_new = xp;
                xp_new[1] = xp[1] * p_new / price_scale;

                T new_D   = MathOps<T>::newton_D(
                    _A_gamma[0], _A_gamma[1], xp_new, _D
                );
                T new_xcp = _xcp(new_D, p_new);
                T new_virtual_price = PoolTraits<T>::PRECISION() * new_xcp / total_supply;

                // Burn donations to reach the LP-protected goal vp.
                T donation_shares_to_burn = PoolTraits<T>::ZERO();
                T goal_vp = PoolTraits<T>::max(lp_repeg_floor, vp);
                if (new_virtual_price < goal_vp) {
                    // What the total supply would be at goal_vp and new_xcp
                    T tweaked_supply = (PoolTraits<T>::PRECISION() * new_xcp) / goal_vp;
                    if (!(tweaked_supply < total_supply)) {
                        throw std::runtime_error("tweaked supply must shrink");
                    }
                    const T donation_shares_needed = total_supply - tweaked_supply;
                    donation_shares_to_burn = PoolTraits<T>::min(
                        donation_shares_needed,
                        donation_unlocked
                    );
                    new_virtual_price = (
                        PoolTraits<T>::PRECISION() * new_xcp
                    ) / (total_supply - donation_shares_to_burn);
                }

                const bool lp_gate_passed =
                    new_virtual_price > PoolTraits<T>::PRECISION() &&
                    new_virtual_price >= lp_repeg_floor;

                // Commit only when the LP-protected threshold is preserved
                if (lp_gate_passed) {
                    // vy asserts after its writes but reverts atomically;
                    // cpp checks before writing anything.
                    _assert_balance(xp_new);

                    D = new_D;
                    virtual_price      = new_virtual_price;
                    cached_price_scale = p_new;

                    if (donation_shares_to_burn > PoolTraits<T>::ZERO()) {
                        // Carry last_donation_release_ts forward so that
                        // _donation_shares() drops by exactly the burn.
                        T shares_unlocked  = _donation_shares(false);
                        T shares_available = donation_unlocked;
                        T shares_unlocked_new = shares_unlocked
                            - (donation_shares_to_burn * shares_unlocked) / shares_available;

                        T new_total = donation_shares - donation_shares_to_burn;
                        T new_elapsed = PoolTraits<T>::ZERO();
                        if (new_total > PoolTraits<T>::ZERO() && shares_unlocked_new > PoolTraits<T>::ZERO()) {
                            new_elapsed = (shares_unlocked_new * donation_duration) / new_total;
                        }

                        donation_shares = new_total;
                        totalSupply    -= donation_shares_to_burn;
                        last_donation_release_ts = T(block_timestamp) - new_elapsed;
                    }
                    _update_policy_state(
                        xp_new, p_new, price_oracle, last_prices,
                        new_virtual_price, xcp_profit, new_D
                    );
                    return p_new;
                }
            }
        }

        // No price adjustment: commit the vp and D computed above.
        _assert_balance(xp);
        D = _D;
        virtual_price = vp;
        _update_policy_state(
            xp, price_scale, price_oracle, last_prices,
            virtual_price, xcp_profit, D
        );
        return price_scale;
    }

    // Views
    T donation_unlocked(bool donation_protection = true) const {
        return _donation_shares(donation_protection);
    }

    T get_virtual_price() const {
        return (virtual_price == Traits::ZERO()) ? Traits::PRECISION() : virtual_price;
    }

    // Exact read-only mirror of TwocryptoView.lp_price at `now`: live xcp
    // virtual price plus the projected native oracle. Used by the LT donation
    // slippage gate without mutating the pool or its EMA cache.
    T lp_price_at(uint64_t now) const {
        if (totalSupply == Traits::ZERO()) {
            throw std::runtime_error("lp_price on empty pool");
        }
        const T live_virtual_price = Traits::PRECISION()
            * _xcp(D, cached_price_scale) / totalSupply;
        const T oracle = projected_price_oracle_at(now);
        T sqrt_oracle{};
        if constexpr (std::is_same_v<T, uint256>) {
            sqrt_oracle = boost::multiprecision::sqrt(
                oracle * Traits::PRECISION()
            );
        } else {
            sqrt_oracle = std::sqrt(oracle * Traits::PRECISION());
        }
        return T(2) * live_virtual_price * sqrt_oracle / Traits::PRECISION();
    }

    T get_p() const {
        if (balances[0] == Traits::ZERO() || balances[1] == Traits::ZERO()) {
            return cached_price_scale;
        }
        return last_prices;
    }

    T get_vp_boosted() const {
        T xcp = _xcp(D, cached_price_scale);
        T donation_unlocked = _donation_shares();
        T locked_supply     = totalSupply - donation_unlocked;
        return (locked_supply == Traits::ZERO()) ? Traits::PRECISION() : (PoolTraits<T>::PRECISION() * xcp / locked_supply);
    }

    // Testing helpers
    void initialize_policy_state_from_pool() {
        if (policy.kind != PolicyKind::None) {
            const auto xp = _xp(balances, cached_price_scale);
            _update_policy_state(
                xp,
                cached_price_scale,
                cached_price_oracle,
                last_prices,
                virtual_price,
                xcp_profit,
                D
            );
        }
    }

    void refresh_policy_context() {
        if (policy.kind != PolicyKind::None) {
            policy.prepare_price_scale_call(block_timestamp, cached_price_oracle);
        }
    }

    void set_block_timestamp(uint64_t ts) {
        block_timestamp = ts;
        policy.set_block_timestamp(ts);
        if (D == Traits::ZERO() && totalSupply == Traits::ZERO()) {
            last_timestamp = ts;
        }
    }

    void advance_time(uint64_t seconds) {
        set_block_timestamp(block_timestamp + seconds);
    }
};

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
