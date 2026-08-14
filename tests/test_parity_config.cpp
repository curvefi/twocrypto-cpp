#include <cassert>
#include <stdexcept>
#include <string>

#include <boost/json.hpp>

#include "parity/pool_config.hpp"

namespace json = boost::json;
using arb::parity::PoolConfig;
using arb::parity::parse_pool_config;
using arb::pools::twocrypto_fx::uint256;

namespace {

json::object exact_config() {
    return json::parse(R"json({
        "name": "exact",
        "A": "400000",
        "gamma": "145000000000000",
        "mid_fee": "26000000",
        "out_fee": "45000000",
        "fee_gamma": "230000000000000",
        "adjustment_step_min": "1000000000000",
        "adjustment_step_max": "1000000000000000",
        "ma_time": "866",
        "reserved_profit_fraction": "5000000000",
        "admin_fee": "5000000000",
        "initial_price": "1000000000000000000",
        "initial_liquidity": [
            "10000000000000000000000",
            "10000000000000000000000"
        ],
        "policy": {
            "kind": "fixed_fee",
            "fee_bps": "17",
            "params": ["7", "9000000000000000001"]
        }
    })json").as_object();
}

void test_valid_exact_config() {
    const PoolConfig<uint256> config = parse_pool_config<uint256>(exact_config());
    if (config.name != "exact" ||
        config.A != uint256("400000") ||
        config.gamma != uint256("145000000000000") ||
        config.mid_fee != uint256("26000000") ||
        config.initial_price != uint256("1000000000000000000") ||
        config.initial_liquidity[0] != uint256("10000000000000000000000") ||
        config.initial_liquidity[1] != uint256("10000000000000000000000") ||
        config.policy_config.params[0] != uint256("7") ||
        config.policy_config.params[1] != uint256("9000000000000000001") ||
        config.policy_config.n_params != 2 ||
        config.policy_kind != arb::pools::twocrypto_fx::PolicyKind::FixedFee ||
        config.policy_config.fee != uint256("17000000")) {
        throw std::runtime_error("valid exact config assertion failed");
    }
}

template <typename Mutator>
void expect_rejected(Mutator mutator, const std::string& expected) {
    auto config = exact_config();
    mutator(config);
    bool threw = false;
    try {
        (void)parse_pool_config<uint256>(config);
    } catch (const std::exception& error) {
        threw = true;
        if (std::string(error.what()).find(expected) == std::string::npos) {
            throw std::runtime_error(
                "expected rejection containing '" + expected + "', got: " + error.what()
            );
        }
    }
    if (!threw) {
        throw std::runtime_error("expected parity config rejection for: " + expected);
    }
}

void test_stale_and_unknown_rejection() {
    expect_rejected(
        [](json::object& config) { config["fee_model_name"] = "legacy"; },
        "legacy pool config fields are not supported"
    );
    expect_rejected(
        [](json::object& config) { config["unknown_application_field"] = "ignored"; },
        "unknown pool config field"
    );
}

} // namespace

int main() {
    test_valid_exact_config();
    test_stale_and_unknown_rejection();
    return 0;
}
