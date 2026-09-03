#include <array>
#include <cmath>
#include <stdexcept>

#include "pools/twocrypto_fx/policies/price_feed.hpp"

namespace fx = arb::pools::twocrypto_fx;
using Policy = fx::ChallengeFeePolicy<double>;

namespace {

void require_near(double actual, double expected, const char* message) {
    if (std::abs(actual - expected) > 1e-14) throw std::runtime_error(message);
}

} // namespace

int main() {
    fx::PolicyConfig<double> params{};
    fx::PolicyPoolConfig<double> pool_config{};
    fx::PolicyResearchContext<double> research{100, 1.0, 1.003, 100};
    Policy::State state{1.0};

    require_near(
        Policy::get_price_scale(state, research, params, pool_config),
        1.015,
        "upward feed gap was not multiplied by five"
    );
    research.price_feed = 0.997;
    require_near(
        Policy::get_price_scale(state, research, params, pool_config),
        0.985,
        "downward feed gap was not multiplied by five"
    );
    research.price_feed = 0.5;
    require_near(
        Policy::get_price_scale(state, research, params, pool_config),
        0.2,
        "large downward feed gap did not saturate at a positive target"
    );

    const std::array<double, 2> xp{1.0, 1.0};
    const fx::PolicyUpdate<double> update{
        xp, 1.002, 1.0, 1.0, 1.0, 1.0, 2.0, 100
    };
    Policy::update_state(state, research, params, pool_config, update);
    research.price_feed = 1.003;
    require_near(
        Policy::get_price_scale(state, research, params, pool_config),
        1.007,
        "policy did not use the latest committed price scale"
    );
    return 0;
}
