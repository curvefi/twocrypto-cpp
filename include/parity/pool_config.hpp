// Private exact uint256 pool/action fixture adapter for the parity target.
#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <boost/json.hpp>

#include "parity/json_utils.hpp"
#include "pools/twocrypto_fx/twocrypto.hpp"

namespace arb::parity {

namespace json = boost::json;
using arb::pools::twocrypto_fx::PolicyConfig;
using arb::pools::twocrypto_fx::PolicyKind;
using arb::pools::twocrypto_fx::PoolTraits;
using arb::pools::twocrypto_fx::TwoCryptoPool;
using arb::pools::twocrypto_fx::uint256;

// This is deliberately not PoolInit: it contains no application scenario,
// cost, donation schedule, tag, or request-echo state.
struct HistoricalState {
    bool enabled{false};
    uint64_t source_block{0};
    uint64_t source_timestamp{0};

    std::array<uint256, 2> balances{uint256(0), uint256(0)};
    std::array<uint256, 2> admin_balances{uint256(0), uint256(0)};
    uint256 D{uint256(0)};
    uint256 total_supply{uint256(0)};
    uint256 price_scale{uint256(0)};
    uint256 price_oracle{uint256(0)};
    uint256 last_prices{uint256(0)};
    uint64_t last_timestamp{0};
    uint256 virtual_price{uint256(0)};
    uint256 xcp_profit{uint256(0)};
    uint256 lp_xcp_profit{uint256(0)};

    uint256 donation_shares{uint256(0)};
    uint256 last_donation_release_ts{uint256(0)};
    uint256 donation_protection_expiry_ts{uint256(0)};
    uint256 donation_protection_period{uint256(0)};
    uint256 donation_protection_lp_threshold{uint256(0)};
    uint256 donation_protection_extension_remainder{uint256(0)};
    uint256 donation_shares_max_ratio{uint256(0)};
};

struct PoolConfig {
    std::string name;
    std::array<uint256, 2> precisions{PoolTraits<uint256>::ONE(), PoolTraits<uint256>::ONE()};
    uint256 A{uint256(0)};
    uint256 gamma{uint256(0)};
    uint256 mid_fee{uint256(0)};
    uint256 out_fee{uint256(0)};
    uint256 fee_gamma{uint256(0)};
    uint256 adjustment_step_min{uint256(0)};
    uint256 adjustment_step_max{uint256(0)};
    uint256 ma_time{uint256(0)};
    uint256 reserved_profit_fraction{PoolTraits<uint256>::FEE_PRECISION() / 2};
    uint256 admin_fee{PoolTraits<uint256>::FEE_PRECISION() / 2};
    PolicyKind policy_kind{PolicyKind::None};
    PolicyConfig<uint256> policy_config{};
    uint256 initial_price{PoolTraits<uint256>::PRECISION()};
    std::array<uint256, 2> initial_liquidity{uint256(0), uint256(0)};
    uint64_t start_timestamp{0};
    HistoricalState historical_state{};
};

struct Action {
    bool is_object{true};
    std::string type;
    std::string parse_error;
    int64_t i{0};
    int64_t j{0};
    int64_t seconds{0};
    int64_t timestamp{0};
    std::string dx;
    std::string amount;
    std::array<std::string, 2> amounts{};
    std::array<std::string, 2> min_amounts{};
    bool has_seconds{false};
    bool has_timestamp{false};
    bool has_amounts{false};
    bool has_min_amounts{false};
    bool donation{false};
};

struct ActionSequence {
    std::string name;
    std::string parse_error;
    uint64_t start_timestamp{0};
    std::vector<Action> actions;
};

inline const json::value& required_value(const json::object& object, const char* key) {
    const auto* value = object.if_contains(key);
    if (value == nullptr) {
        throw std::runtime_error(std::string("missing key: ") + key);
    }
    return *value;
}

inline void require_scalar(const json::value& value, const char* context) {
    if (!is_number_or_string(value)) {
        throw std::runtime_error(std::string(context) + " must be a number or string");
    }
}

uint256 parse_config_plain(const json::value& value) {
    require_scalar(value, "pool config value");
    return uint256(scalar_to_string(value));
}

uint256 parse_config_wad(const json::value& value) {
    require_scalar(value, "pool config WAD value");
    return uint256(scalar_to_string(value));
}

uint256 parse_config_fee(const json::value& value) {
    require_scalar(value, "pool config fee value");
    return uint256(scalar_to_string(value));
}

uint256 parse_historical_wad(const json::value& value) {
    require_scalar(value, "historical_state WAD value");
    return uint256(scalar_to_string(value));
}

uint256 parse_historical_plain(const json::value& value) {
    require_scalar(value, "historical_state value");
    return uint256(scalar_to_string(value));
}

PolicyConfig<uint256> parse_policy_config(const json::value& policy) {
    PolicyConfig<uint256> config{};
    if (policy.is_string()) {
        config.kind = arb::pools::twocrypto_fx::policy_kind_from_string(
            std::string(policy.as_string().c_str())
        );
        return config;
    }
    if (!policy.is_object()) {
        throw std::runtime_error("pool policy must be a string or object");
    }

    const auto& object = policy.as_object();
    for (const auto& entry : object) {
        const std::string_view key(entry.key().data(), entry.key().size());
        if (key == "kind" || key == "price_source" || key == "params" ||
            key == "fee" || key == "fee_bps") {
            continue;
        }
        if (key == "price_source_ema_half_time") {
            throw std::runtime_error(
                "policy price_source_ema_half_time is no longer supported"
            );
        }
        throw std::runtime_error("unknown pool policy field: " + std::string(key));
    }

    std::string kind = "none";
    if (auto* value = object.if_contains("kind")) {
        if (!value->is_string()) {
            throw std::runtime_error("policy kind must be a string");
        }
        kind = std::string(value->as_string().c_str());
    }
    config.kind = arb::pools::twocrypto_fx::policy_kind_from_string(kind);

    if (auto* source = object.if_contains("price_source")) {
        if (!source->is_string()) {
            throw std::runtime_error("pool policy price_source must be a string");
        }
        const std::string value(source->as_string().c_str());
        if (!value.empty() && value != "cex" && value != "event_p_cex") {
            throw std::runtime_error("external policy price_source is no longer supported");
        }
    }

    if (auto* params = object.if_contains("params")) {
        if (!params->is_array()) {
            throw std::runtime_error("pool policy params must be an array of numbers");
        }
        const auto& values = params->as_array();
        if (values.size() > config.params.size()) {
            throw std::runtime_error("pool policy params: too many entries");
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (!is_number_or_string(values[i])) {
                throw std::runtime_error(
                    "pool policy params entries must be numbers or strings"
                );
            }
            config.params[i] = uint256(scalar_to_string(values[i]));
        }
        config.n_params = values.size();
    }
    if (auto* fee = object.if_contains("fee")) {
        if (!is_number_or_string(*fee)) {
            throw std::runtime_error("pool policy fee must be a string or number");
        }
        config.fee = parse_config_fee(*fee);
    } else if (auto* fee_bps = object.if_contains("fee_bps")) {
        if (!is_number_or_string(*fee_bps)) {
            throw std::runtime_error("pool policy fee_bps must be a string or number");
        }
        config.fee = uint256(scalar_to_string(*fee_bps)) *
                PoolTraits<uint256>::FEE_PRECISION() / uint256(10000);
    }
    return config;
}

HistoricalState parse_historical_state(const json::value& value) {
    if (!value.is_object()) {
        throw std::runtime_error("pool historical_state must be an object");
    }
    const auto& object = value.as_object();
    HistoricalState state{};
    state.enabled = true;
    state.source_block = get_u64_opt(object, "source_block", 0);
    state.source_timestamp = get_u64_opt(object, "source_timestamp", 0);

    const auto parse_pair = [&](const char* key) {
        const auto& raw = required_value(object, key);
        if (!raw.is_array() || raw.as_array().size() != 2) {
            throw std::runtime_error(
                std::string("pool historical_state ") + key +
                " must have exactly two entries"
            );
        }
        const auto& values = raw.as_array();
        return std::array<uint256, 2>{
            parse_historical_wad(values[0]),
            parse_historical_wad(values[1]),
        };
    };

    state.balances = parse_pair("balances");
    state.admin_balances = parse_pair("admin_balances");
    state.D = parse_historical_wad(required_value(object, "D"));
    state.total_supply = parse_historical_wad(required_value(object, "total_supply"));
    state.price_scale = parse_historical_wad(required_value(object, "price_scale"));
    state.price_oracle = parse_historical_wad(required_value(object, "price_oracle"));
    state.last_prices = parse_historical_wad(required_value(object, "last_prices"));
    state.last_timestamp = get_u64_opt(object, "last_timestamp", 0);
    state.virtual_price = parse_historical_wad(required_value(object, "virtual_price"));
    state.xcp_profit = parse_historical_wad(required_value(object, "xcp_profit"));
    state.lp_xcp_profit = parse_historical_wad(required_value(object, "lp_xcp_profit"));
    state.donation_shares = parse_historical_wad(required_value(object, "donation_shares"));
    state.last_donation_release_ts = parse_historical_plain(required_value(object, "last_donation_release_ts"));
    state.donation_protection_expiry_ts = parse_historical_plain(required_value(object, "donation_protection_expiry_ts"));
    state.donation_protection_period = parse_historical_plain(required_value(object, "donation_protection_period"));
    state.donation_protection_lp_threshold = parse_historical_wad(required_value(object, "donation_protection_lp_threshold"));
    state.donation_protection_extension_remainder = parse_historical_wad(required_value(object, "donation_protection_extension_remainder"));
    state.donation_shares_max_ratio = parse_historical_wad(required_value(object, "donation_shares_max_ratio"));

    if (state.source_timestamp == 0 || state.last_timestamp == 0) {
        throw std::runtime_error("pool historical_state requires nonzero source_timestamp and last_timestamp");
    }
    if (!(state.D > uint256(0)) || !(state.total_supply > uint256(0)) ||
        !(state.price_scale > uint256(0)) || !(state.price_oracle > uint256(0)) ||
        !(state.virtual_price > uint256(0))) {
        throw std::runtime_error("pool historical_state requires positive D, supply, prices, and virtual_price");
    }
    return state;
}

PoolConfig parse_pool_config(const json::object& object) {
    PoolConfig config{};
    config.name = get_str(object, "name");

    const auto parse_plain = [&](const char* key) {
        return parse_config_plain(required_value(object, key));
    };
    const auto parse_wad = [&](const char* key) {
        return parse_config_wad(required_value(object, key));
    };
    const auto parse_fee = [&](const char* key) {
        return parse_config_fee(required_value(object, key));
    };

    config.A = parse_plain("A");
    config.gamma = parse_wad("gamma");
    config.mid_fee = parse_fee("mid_fee");
    config.out_fee = parse_fee("out_fee");
    config.fee_gamma = parse_wad("fee_gamma");

    if (object.if_contains("allowed_extra_profit") ||
        object.if_contains("adjustment_step") || object.if_contains("lp_profit_fraction") ||
        object.if_contains("fee_params") || object.if_contains("fee_model_name")) {
        throw std::runtime_error("legacy pool config fields are not supported");
    }

    config.adjustment_step_min = parse_wad("adjustment_step_min");
    config.adjustment_step_max = parse_wad("adjustment_step_max");
    config.ma_time = parse_plain("ma_time");
    if (auto* value = object.if_contains("reserved_profit_fraction")) {
        config.reserved_profit_fraction = parse_config_fee(*value);
    }
    if (auto* value = object.if_contains("admin_fee")) {
        config.admin_fee = parse_config_fee(*value);
    }
    if (auto* value = object.if_contains("policy")) {
        config.policy_config = parse_policy_config(*value);
        config.policy_kind = config.policy_config.kind;
    }
    config.initial_price = parse_wad("initial_price");

    const auto* liquidity = object.if_contains("initial_liquidity");
    if (liquidity == nullptr || !liquidity->is_array()) {
        throw std::runtime_error("missing/invalid initial_liquidity");
    }
    const auto& values = liquidity->as_array();
    if (values.size() < 2 || !values[0].is_string() || !values[1].is_string()) {
        throw std::runtime_error("initial_liquidity must be [str,str]");
    }
    config.initial_liquidity = {
        parse_config_wad(values[0]),
        parse_config_wad(values[1]),
    };

    if (auto* value = object.if_contains("start_timestamp")) {
        config.start_timestamp = static_cast<uint64_t>(parse_config_plain(*value));
        if (config.start_timestamp > 10000000000ULL) {
            config.start_timestamp /= 1000ULL;
        }
    }
    if (auto* value = object.if_contains("historical_state")) {
        config.historical_state = parse_historical_state(*value);
        if (config.start_timestamp != 0 &&
            config.start_timestamp < config.historical_state.source_timestamp) {
            throw std::runtime_error("pool start_timestamp predates historical_state source_timestamp");
        }
    }

    for (const auto& entry : object) {
        const std::string_view key(entry.key().data(), entry.key().size());
        if (key == "name" || key == "A" || key == "gamma" || key == "mid_fee" ||
            key == "out_fee" || key == "fee_gamma" || key == "adjustment_step_min" ||
            key == "adjustment_step_max" || key == "ma_time" ||
            key == "reserved_profit_fraction" || key == "admin_fee" || key == "policy" ||
            key == "initial_price" || key == "initial_liquidity" ||
            key == "start_timestamp" || key == "historical_state") {
            continue;
        }
        throw std::runtime_error("unknown pool config field: " + std::string(key));
    }
    return config;
}

PoolConfig parse_pool_config(const json::value& value) {
    if (!value.is_object()) {
        throw std::runtime_error("pool config must be an object");
    }
    return parse_pool_config(value.as_object());
}

TwoCryptoPool<uint256> make_pool(const PoolConfig& config, const ActionSequence& sequence) {
    const uint64_t start_ts = sequence.start_timestamp != 0
        ? sequence.start_timestamp
        : config.start_timestamp;
    if (config.historical_state.enabled &&
        start_ts != 0 &&
        start_ts < config.historical_state.source_timestamp) {
        throw std::runtime_error("run start precedes historical pool checkpoint");
    }
    TwoCryptoPool<uint256> pool(
        config.precisions,
        config.A,
        config.gamma,
        config.mid_fee,
        config.out_fee,
        config.fee_gamma,
        config.adjustment_step_min,
        config.adjustment_step_max,
        config.ma_time,
        config.initial_price,
        config.reserved_profit_fraction,
        config.admin_fee,
        config.policy_kind,
        config.policy_config
    );
    if (start_ts > 0) {
        pool.set_block_timestamp(start_ts);
    }

    if (config.historical_state.enabled) {
        const auto& state = config.historical_state;
        pool.balances = state.balances;
        pool.admin_balances = state.admin_balances;
        pool.D = state.D;
        pool.totalSupply = state.total_supply;
        pool.cached_price_scale = state.price_scale;
        pool.cached_price_oracle = state.price_oracle;
        pool.last_prices = state.last_prices;
        pool.last_timestamp = state.last_timestamp;
        pool.virtual_price = state.virtual_price;
        pool.xcp_profit = state.xcp_profit;
        pool.lp_xcp_profit = state.lp_xcp_profit;
        pool.donation_shares = state.donation_shares;
        pool.last_donation_release_ts = state.last_donation_release_ts;
        pool.donation_protection_expiry_ts = state.donation_protection_expiry_ts;
        pool.donation_protection_period = state.donation_protection_period;
        pool.donation_protection_lp_threshold = state.donation_protection_lp_threshold;
        pool.donation_protection_extension_remainder = state.donation_protection_extension_remainder;
        pool.donation_shares_max_ratio = state.donation_shares_max_ratio;
        pool.cached_ema_dt = 0;
        pool.cached_ema_alpha = uint256(0);
        pool.cached_ema_alpha_valid = false;
        pool.initialize_policy_state_from_pool();
    } else {
        (void)pool.add_liquidity(
            config.initial_liquidity,
            PoolTraits<uint256>::ZERO(),
            /*donation=*/false
        );
    }
    return pool;
}

inline int64_t required_i64(const json::object& object, const char* key) {
    const auto& value = required_value(object, key);
    if (value.is_int64()) return value.as_int64();
    if (value.is_uint64()) return static_cast<int64_t>(value.as_uint64());
    throw std::runtime_error(std::string("expected integer for key: ") + key);
}

inline std::array<std::string, 2> required_string_pair(
    const json::object& object,
    const char* key
) {
    const auto& value = required_value(object, key);
    if (!value.is_array() || value.as_array().size() < 2 ||
        !value.as_array()[0].is_string() || !value.as_array()[1].is_string()) {
        throw std::runtime_error(std::string(key) + " must be [str,str]");
    }
    return {
        std::string(value.as_array()[0].as_string().c_str()),
        std::string(value.as_array()[1].as_string().c_str()),
    };
}

inline Action parse_action(const json::value& value) {
    Action action{};
    if (!value.is_object()) {
        action.is_object = false;
        action.parse_error = "action must be object";
        return action;
    }
    const auto& object = value.as_object();
    try {
        action.type = get_str(object, "type");
        if (action.type == "exchange") {
            action.i = required_i64(object, "i");
            action.j = required_i64(object, "j");
            action.dx = get_str(object, "dx");
        } else if (action.type == "add_liquidity") {
            action.amounts = required_string_pair(object, "amounts");
            action.has_amounts = true;
            if (auto* donation = object.if_contains("donation")) {
                if (!donation->is_bool()) throw std::runtime_error("expected bool for key: donation");
                action.donation = donation->as_bool();
            }
        } else if (action.type == "remove_liquidity") {
            action.amount = get_str(object, "amount");
            if (object.if_contains("min_amounts") != nullptr) {
                action.min_amounts = required_string_pair(object, "min_amounts");
                action.has_min_amounts = true;
            }
        } else if (action.type == "time_travel") {
            if (object.if_contains("seconds")) {
                action.seconds = required_i64(object, "seconds");
                action.has_seconds = true;
            } else if (object.if_contains("timestamp")) {
                action.timestamp = required_i64(object, "timestamp");
                action.has_timestamp = true;
            }
        }
    } catch (const std::exception& error) {
        action.parse_error = error.what();
    }
    return action;
}

inline ActionSequence parse_action_sequence(const json::object& object) {
    ActionSequence sequence;
    sequence.name = get_str(object, "name");
    sequence.start_timestamp = get_u64_opt(object, "start_timestamp", 0);
    const auto* raw_actions = object.if_contains("actions");
    if (raw_actions == nullptr || !raw_actions->is_array()) {
        sequence.parse_error = "sequence.actions missing/invalid";
        return sequence;
    }
    const auto& values = raw_actions->as_array();
    sequence.actions.reserve(values.size());
    for (const auto& value : values) {
        sequence.actions.push_back(parse_action(value));
    }
    return sequence;
}


} // namespace arb::parity
