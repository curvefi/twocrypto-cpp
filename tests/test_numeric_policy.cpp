#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "pools/twocrypto_fx/twocrypto.hpp"

namespace fx = arb::pools::twocrypto_fx;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_long_double_ema_precision() {
    using T = long double;
    constexpr uint64_t start = 100;
    const T ma_time = T(1.000000000000000111L);
    const T old_oracle = T(1.25L);
    const T observation = T(0.75L);
    using std::exp;
    const T alpha = exp(-T(1) / ma_time);
    const T expected = observation * (T(1) - alpha) + old_oracle * alpha;

    fx::TwoCryptoPool<T> pool(
        {T(1), T(1)}, T(400000), T(0.000145), T(0.0026), T(0.0045),
        T(0.00023), T(0.000001), T(0.1), T(866), T(1.0)
    );
    pool.set_block_timestamp(start);
    (void)pool.add_liquidity({T(10000), T(10000)}, T(0));
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

} // namespace

int main() {
    test_long_double_ema_precision();
    return 0;
}
