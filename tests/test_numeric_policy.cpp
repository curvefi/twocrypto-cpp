#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#include "pools/twocrypto_fx/twocrypto.hpp"

namespace fx = arb::pools::twocrypto_fx;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename T>
T value(const char* integer_value, long double floating_value) {
    if constexpr (std::is_same_v<T, fx::uint256>) {
        return T(integer_value);
    } else {
        return T(floating_value);
    }
}

template <typename T>
fx::TwoCryptoPool<T> make_pool(fx::PolicyKind kind) {
    return fx::TwoCryptoPool<T>(
        {T(1), T(1)},
        value<T>("400000", 400000.0L),
        value<T>("145000000000000", 0.000145L),
        value<T>("26000000", 0.0026L),
        value<T>("45000000", 0.0045L),
        value<T>("230000000000000", 0.00023L),
        value<T>("1000000000000", 0.000001L),
        value<T>("100000000000000000", 0.1L),
        T(866),
        value<T>("1000000000000000000", 1.0L),
        value<T>("5000000000", 0.5L),
        value<T>("5000000000", 0.5L),
        kind
    );
}

template <typename T>
bool same_economic_state(
    const fx::TwoCryptoPool<T>& a,
    const fx::TwoCryptoPool<T>& b
) {
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
        a.donation_shares == b.donation_shares &&
        a.last_donation_release_ts == b.last_donation_release_ts;
}

template <typename T>
void test_passthrough_matches_native() {
    constexpr uint64_t start = 1'700'000'000;
    const T liquidity = value<T>("10000000000000000000000", 10000.0L);
    const T trade = value<T>("1000000000000000000", 1.0L);

    auto native = make_pool<T>(fx::PolicyKind::None);
    auto passthrough = make_pool<T>(fx::PolicyKind::Compiled);
    native.set_block_timestamp(start);
    passthrough.set_block_timestamp(start);
    require(
        native.add_liquidity({liquidity, liquidity}, T(0)) ==
            passthrough.add_liquidity({liquidity, liquidity}, T(0)),
        "passthrough changed initial liquidity"
    );
    native.last_timestamp = start;
    passthrough.last_timestamp = start;

    const std::array<T, 2> balanced_xp{liquidity, liquidity};
    require(
        passthrough.policy.get_fee(balanced_xp) == T(0),
        "passthrough fee override must be zero"
    );
    passthrough.policy.prepare_price_scale_call(start, passthrough.cached_price_oracle);
    require(
        passthrough.policy.get_price_scale() == T(0),
        "passthrough price-scale override must be zero"
    );
    require(
        native.fee(balanced_xp) == passthrough.fee(balanced_xp),
        "passthrough changed native fee fallback"
    );

    native.set_block_timestamp(start + 97);
    passthrough.set_block_timestamp(start + 97);
    require(
        native.exchange(T(0), T(1), trade, T(0)) ==
            passthrough.exchange(T(0), T(1), trade, T(0)),
        "passthrough changed exchange result"
    );
    require(
        same_economic_state(native, passthrough),
        "passthrough changed post-exchange economic state"
    );

    native.set_block_timestamp(start + 211);
    passthrough.set_block_timestamp(start + 211);
    native.tick();
    passthrough.tick();
    require(
        same_economic_state(native, passthrough),
        "passthrough changed native EMA or price-scale state"
    );
}

template <typename T>
void test_floating_ema_uses_pool_type() {
    static_assert(std::is_floating_point_v<T>);
    constexpr uint64_t start = 100;
    const T ma_time = T(1.000000000000000111L);
    const T old_oracle = T(1.25L);
    const T observation = T(0.75L);
    using std::exp;
    const T alpha = exp(-T(1) / ma_time);
    const T expected = observation * (T(1) - alpha) + old_oracle * alpha;

    typename fx::NativeFeePolicy<T>::State policy_state{};
    policy_state.pool.price_scale = T(1);
    policy_state.pool.price_oracle = old_oracle;
    policy_state.pool.last_prices = observation;
    policy_state.pool.ts = start;
    fx::PolicyResearchContext<T> research{start + 1, T(0)};
    fx::PolicyConfig<T> params{};
    fx::PolicyPoolConfig<T> config{};
    config.ma_time = ma_time;
    config.precision = T(1);
    require(
        fx::NativeFeePolicy<T>::get_price_scale(
            policy_state, research, params, config
        ) == expected,
        "native policy EMA narrowed its floating-point type"
    );

    auto pool = make_pool<T>(fx::PolicyKind::None);
    const T liquidity = T(10000);
    pool.set_block_timestamp(start);
    (void)pool.add_liquidity({liquidity, liquidity}, T(0));
    pool.ma_time = ma_time;
    pool.cached_price_oracle = old_oracle;
    pool.last_prices = observation;
    pool.last_timestamp = start;
    pool.cached_ema_alpha_valid = false;
    pool.set_block_timestamp(start + 1);
    require(
        pool.projected_price_oracle_at(start + 1) == expected,
        "projected pool EMA narrowed its floating-point type"
    );
    pool.tick();
    require(pool.cached_ema_alpha == alpha, "committed pool EMA narrowed its type");

    if constexpr (std::numeric_limits<T>::digits >
                  std::numeric_limits<double>::digits) {
        const T narrowed_alpha = T(std::exp(
            -static_cast<double>(T(1)) / static_cast<double>(ma_time)
        ));
        require(alpha != narrowed_alpha, "EMA fixture did not expose double narrowing");
    }
}

void test_descriptor_api() {
    constexpr fx::PolicyDescriptor<1> descriptor{
        "bounded_probe",
        {{{"half_life", 0, "seconds",
           3600.0L, 60.0L, 86400.0L, 1.0L}}},
    };
    static_assert(fx::POLICY_DESCRIPTOR_ABI_VERSION == 1);
    static_assert(descriptor.size() == 1);
    static_assert(descriptor.parameters[0].order == 0);
    static_assert(descriptor.parameters[0].minimum == 60.0L);
    using Passthrough = fx::ChallengeFeePolicy<double>;
    static_assert(Passthrough::PARAM_COUNT == Passthrough::DESCRIPTOR.size());
    static_assert(Passthrough::PARAM_COUNT == 0);
    require(
        Passthrough::DESCRIPTOR.name == std::string_view(Passthrough::NAME),
        "legacy policy name diverged from descriptor"
    );
}

} // namespace

int main() {
    test_descriptor_api();
    test_passthrough_matches_native<fx::uint256>();
    test_passthrough_matches_native<double>();
    test_passthrough_matches_native<long double>();
    test_floating_ema_uses_pool_type<double>();
    test_floating_ema_uses_pool_type<long double>();
    return 0;
}
