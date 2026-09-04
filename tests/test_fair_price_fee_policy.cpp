#include <array>
#include <cmath>
#include <stdexcept>

#include "pools/twocrypto_fx/policies/fair_price_fee.hpp"

namespace fx = arb::pools::twocrypto_fx;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename T>
fx::PolicyPoolConfig<T> pool_config(
    const T& amp,
    const T& precision,
    const T& fee_precision
) {
    fx::PolicyPoolConfig<T> config{};
    config.A = amp;
    config.precision = precision;
    config.fee_precision = fee_precision;
    return config;
}

void test_uint_matches_python_vyper_lattice_reference() {
    using T = fx::uint256;
    using Policy = fx::ChallengeFeePolicy<T>;

    const T precision("1000000000000000000");
    const T fee_precision("10000000000");
    const auto config = pool_config(T(400000), precision, fee_precision);
    fx::PolicyConfig<T> params{};
    params.params[0] = T(100000000);  // 1%
    params.params[1] = T("500000000000000000");
    params.n_params = 2;

    typename Policy::State state{
        {T("500000000000000000000000"), T("500000000000000000000000")},
        T("100000000000000000000000"),
        T("1000000000000000000000000"),
    };
    const std::array<T, 2> xp_new{
        T("690189784785674418049882"),
        T("311830109271451328536310"),
    };
    fx::PolicyResearchContext<T> research{};
    research.block_timestamp = 100;
    research.price_feed = T("105000000000000000000000");
    research.price_feed_timestamp = 100;

    require(
        Policy::marginal_price(
            state.xp, state.D, config.A, state.price_scale, config.precision
        ) == T("100000000000000000000000"),
        "old marginal price differs from Vyper reference"
    );
    require(
        Policy::marginal_price(
            xp_new, state.D, config.A, state.price_scale, config.precision
        ) == T("102500000000000000000000"),
        "trial marginal price differs from Vyper reference"
    );
    require(
        Policy::get_fee(state, params, config, research, xp_new) == T(236978967),
        "uint fee differs from Python/Vyper lattice reference"
    );
    params.params[1] = precision;
    require(
        Policy::get_fee(state, params, config, research, xp_new) == T(373957934),
        "full capture rounded above the available uint surplus"
    );

    params.params[1] = T("500000000000000000");
    research.price_feed = T("95000000000000000000000");
    const std::array<T, 2> xp_new_down{
        T("308948343212488831071349"),
        T("693144514914106237817018"),
    };
    require(
        Policy::get_fee(state, params, config, research, xp_new_down) == T(247966553),
        "reverse-direction uint fee differs from Python/Vyper lattice reference"
    );
    require(
        Policy::fee_floor(params, config, T(1)) == T(100000000),
        "policy floor is not the base fee"
    );
}

void test_double_capture_and_admission_boundaries() {
    using T = double;
    using Policy = fx::ChallengeFeePolicy<T>;

    const auto config = pool_config(T(400000), T(1), T(1));
    typename Policy::State state{{500000.0, 500000.0}, 100000.0, 1000000.0};
    const std::array<T, 2> xp_new{
        690189.7847856744,
        311830.1092714513,
    };
    fx::PolicyResearchContext<T> research{100, 0.0, 105000.0, 100};
    fx::PolicyConfig<T> params{};
    params.params = {0.01, 0.0};
    params.n_params = 2;

    require(
        std::abs(Policy::get_fee(state, params, config, research, xp_new) - 0.01) < 1e-15,
        "zero capture did not preserve the fixed base fee"
    );
    params.params[1] = 0.5;
    require(
        std::abs(
            Policy::get_fee(state, params, config, research, xp_new) - 0.0236978967
        ) < 1e-9,
        "half capture fee is incorrect"
    );
    params.params[1] = 1.0;
    const T full_capture = Policy::get_fee(state, params, config, research, xp_new);
    require(full_capture > 0.037 && full_capture < 0.038, "full capture fee is incorrect");

    research.price_feed = 95000.0;
    require(
        Policy::get_fee(state, params, config, research, xp_new) == 1.0,
        "wrong-way trade was admitted"
    );
    research.price_feed = 105000.0;
    research.price_feed_timestamp = 99;
    require(
        Policy::get_fee(state, params, config, research, xp_new) == 1.0,
        "stale fair price was admitted"
    );
}

void test_non_swap_falls_back_and_update_stores_only_required_state() {
    using T = double;
    using Policy = fx::ChallengeFeePolicy<T>;

    const auto config = pool_config(T(400000), T(1), T(1));
    fx::PolicyConfig<T> params{};
    params.params = {0.01, 0.5};
    params.n_params = 2;
    typename Policy::State state{{500000.0, 500000.0}, 100000.0, 1000000.0};
    fx::PolicyResearchContext<T> research{100, 0.0, 105000.0, 100};

    require(
        Policy::get_fee(state, params, config, research, state.xp) == 0.0,
        "current-state quote did not use native fallback"
    );
    require(
        Policy::get_fee(
            state, params, config, research, {501000.0, 501000.0}
        ) == 0.0,
        "liquidity operation did not use native fallback"
    );

    const std::array<T, 2> committed{510000.0, 490500.0};
    const fx::PolicyUpdate<T> update{
        committed, 100100.0, 1.0, 1.0, 1.0, 1.0, 1000500.0, 100
    };
    Policy::update_state(state, research, params, config, update);
    require(state.xp == committed, "policy did not store committed xp");
    require(state.price_scale == 100100.0, "policy did not store price scale");
    require(state.D == 1000500.0, "policy did not store D");
    require(
        Policy::get_price_scale(state, research, params, config) == 0.0,
        "fee-only policy overrode the native price-scale target"
    );
}

} // namespace

int main() {
    test_uint_matches_python_vyper_lattice_reference();
    test_double_capture_and_admission_boundaries();
    test_non_swap_falls_back_and_update_stores_only_required_state();
    return 0;
}
