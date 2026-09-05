#include <stdexcept>

#include <boost/json.hpp>

#include "parity/pool_config.hpp"

namespace parity = arb::parity;
using uint256 = arb::pools::twocrypto_fx::uint256;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

boost::json::object historical_cb_btc_config() {
    return boost::json::parse(R"({
        "name":"yb_cbbtc_historical",
        "precisions":["01","010000000000"],
        "A":"50000",
        "gamma":"11111111111",
        "mid_fee":"146000000",
        "out_fee":"170000000",
        "fee_gamma":"54202748000000000",
        "adjustment_step_min":"100000000",
        "adjustment_step_max":"5000000000000000",
        "ma_time":"865",
        "reserved_profit_fraction":"3010101009",
        "admin_fee":"0",
        "policy":{"kind":"none"},
        "initial_price":"65206772515412375157553",
        "initial_liquidity":["41782235588987049147272558","64566532399"],
        "start_timestamp":"1787124419",
        "historical_state":{
            "source_block":"25787653",
            "source_timestamp":"1787124407",
            "balances":["41782235588987049147272558","64566532399"],
            "admin_balances":["0","0"],
            "last_admin_fee_claim_timestamp":"0",
            "D":"83883886069855587132503548",
            "total_supply":"163186165447660981876114",
            "price_scale":"65206772515412375157553",
            "price_oracle":"65187018497237324937197",
            "last_prices":"65128141986231803919891",
            "last_timestamp":"1787119955",
            "virtual_price":"1006511766136368811",
            "xcp_profit":"1019133970053185099",
            "lp_xcp_profit":"1005759518256323877",
            "donation_shares":"9869390969833744038",
            "last_donation_release_ts":"1787119955",
            "donation_protection_expiry_ts":"1787119307",
            "donation_protection_period":"600",
            "donation_protection_lp_threshold":"200000000000000000",
            "donation_protection_extension_remainder":"75319538604638000",
            "donation_shares_max_ratio":"100000000000000000"
        }
    })").as_object();
}

void test_precision_schema_and_raw_cb_btc_quantization() {
    auto object = historical_cb_btc_config();
    const auto config = parity::parse_pool_config(object);
    require(config.precisions == std::array<uint256, 2>{uint256(1), uint256(10'000'000'000ULL)},
            "explicit token precisions were not parsed");

    parity::ActionSequence sequence{};
    sequence.start_timestamp = 1'787'151'911;
    auto pool = parity::make_pool(config, sequence);
    const auto result = pool.exchange(0, 1, uint256("92853005871462460948480"), 0);
    require(result[0] == uint256(140'445'192), "cbBTC output lost raw 8-decimal quantization");

    object.erase("precisions");
    const auto defaults = parity::parse_pool_config(object).precisions;
    require(defaults == std::array<uint256, 2>{uint256(1), uint256(1)},
            "omitted precisions did not preserve the legacy default");

    for (const boost::json::value& invalid : {
             boost::json::value(0), boost::json::value(-1), boost::json::value(1.5),
             boost::json::value("10000000000000000000")}) {
        object["precisions"] = boost::json::array{1, invalid};
        bool rejected = false;
        try {
            (void)parity::parse_pool_config(object);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "non-positive or fractional token precision was accepted");
    }
}

void test_rejects_malformed_optional_and_action_values() {
    auto object = historical_cb_btc_config();
    auto& historical = object["historical_state"].as_object();
    historical.erase("source_block");
    require(parity::parse_pool_config(object).historical_state.source_block == 0,
            "omitted source_block did not use its default");
    historical["source_block"] = boost::json::value("00025787653");
    require(parity::parse_pool_config(object).historical_state.source_block == 25787653,
            "digit string source_block was not retained");
    historical["source_block"] = boost::json::parse(R"("12\u0000bad")");
    bool rejected = false;
    try {
        (void)parity::parse_pool_config(object);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "embedded-NUL source_block was silently truncated");

    historical["source_block"] = boost::json::value("25787653");
    object["initial_liquidity"] = boost::json::array{"1", "2", "3"};
    rejected = false;
    try {
        (void)parity::parse_pool_config(object);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "extra initial_liquidity entry was discarded");

    const auto expect_action_error = [](const char* raw) {
        const auto action = parity::parse_action(boost::json::parse(raw));
        require(!action.parse_error.empty(), "invalid time_travel action was accepted");
    };
    expect_action_error(R"({"type":"time_travel"})");
    expect_action_error(R"({"type":"time_travel","seconds":0,"timestamp":1})");
    expect_action_error(R"({"type":"time_travel","seconds":-1})");
    const auto valid_zero = parity::parse_action(
        boost::json::parse(R"({"type":"time_travel","seconds":0})")
    );
    require(valid_zero.parse_error.empty() && valid_zero.has_seconds && valid_zero.seconds == 0,
            "zero-second time_travel was rejected");
    const auto extra_amount = parity::parse_action(boost::json::parse(
        R"({"type":"add_liquidity","amounts":["1","2","3"]})"
    ));
    require(!extra_amount.parse_error.empty(), "extra action amount was discarded");
}

} // namespace

int main() {
    test_precision_schema_and_raw_cb_btc_quantization();
    test_rejects_malformed_optional_and_action_values();
    return 0;
}
