#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include <boost/json.hpp>

#include "pools/twocrypto_fx/policies/yieldbasis.hpp"

namespace {

namespace json = boost::json;
using Policy = arb::pools::twocrypto_fx::ChallengeFeePolicy<
    arb::pools::twocrypto_fx::uint256>;
using arb::pools::twocrypto_fx::PolicyConfig;
using arb::pools::twocrypto_fx::PolicyPoolConfig;
using arb::pools::twocrypto_fx::PolicyResearchContext;
using arb::pools::twocrypto_fx::PolicyUpdate;
using arb::pools::twocrypto_fx::uint256;

std::string required_string(const json::object& object, const char* key) {
    const auto* value = object.if_contains(key);
    if (value == nullptr || !value->is_string()) {
        throw std::invalid_argument(std::string("missing string: ") + key);
    }
    return std::string(value->as_string().c_str());
}

uint64_t required_u64(const json::object& object, const char* key) {
    return uint256(required_string(object, key)).convert_to<uint64_t>();
}

json::value integer(const uint256& value) {
    return json::value(value.convert_to<std::string>());
}

json::object state_json(const Policy::State& state) {
    return {
        {"last_update_ts", std::to_string(state.last_update_ts)},
        {"last_prices", integer(state.last_prices)},
        {"fast_ema", integer(state.fast_ema)},
        {"slow_ema", integer(state.slow_ema)},
        {"price_scale", integer(state.price_scale)},
    };
}

class Evaluator {
public:
    json::object handle(const json::object& request) {
        const std::string op = required_string(request, "op");
        if (op == "init") return initialize(request);
        require_initialized();
        if (op == "state") return success({{"state", state_json(state_)}});
        if (op == "get_emas") return get_emas(request);
        if (op == "get_price_scale") return get_price_scale(request);
        if (op == "get_fee") return get_fee(request);
        if (op == "update") return update(request);
        throw std::invalid_argument("unsupported operation: " + op);
    }

private:
    Policy::State state_{};
    PolicyConfig<uint256> params_{};
    PolicyPoolConfig<uint256> pool_config_{};
    bool initialized_{false};

    static json::object success(json::object payload = {}) {
        payload["ok"] = true;
        return payload;
    }

    void require_initialized() const {
        if (!initialized_) throw std::logic_error("evaluator is not initialized");
    }

    json::object initialize(const json::object& request) {
        const auto* raw = request.if_contains("params");
        if (raw == nullptr || !raw->is_array()) {
            throw std::invalid_argument("params must be an array");
        }
        const auto& values = raw->as_array();
        params_ = {};
        params_.n_params = values.size();
        if (values.size() > params_.params.size()) {
            throw std::invalid_argument("too many params");
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (!values[i].is_string()) {
                throw std::invalid_argument("params must contain strings");
            }
            params_.params[i] = uint256(std::string(values[i].as_string().c_str()));
        }
        Policy::validate_params(params_);
        state_ = {};
        initialized_ = true;
        return success();
    }

    static uint64_t now(const json::object& request) {
        return required_u64(request, "timestamp");
    }

    json::object get_emas(const json::object& request) {
        const auto values = Policy::get_emas(state_, now(request), params_);
        return success({{"value", json::array{integer(values[0]), integer(values[1])}}});
    }

    json::object get_price_scale(const json::object& request) {
        PolicyResearchContext<uint256> research{};
        research.block_timestamp = now(request);
        return success({{"value", integer(
            Policy::get_price_scale(state_, research, params_, pool_config_)
        )}});
    }

    json::object get_fee(const json::object& request) {
        const auto* raw = request.if_contains("xp");
        if (raw == nullptr || !raw->is_array() || raw->as_array().size() != 2) {
            throw std::invalid_argument("xp must contain two strings");
        }
        const auto& values = raw->as_array();
        if (!values[0].is_string() || !values[1].is_string()) {
            throw std::invalid_argument("xp must contain two strings");
        }
        const std::array<uint256, 2> xp{
            uint256(std::string(values[0].as_string().c_str())),
            uint256(std::string(values[1].as_string().c_str())),
        };
        PolicyResearchContext<uint256> research{};
        research.block_timestamp = now(request);
        return success({{"value", integer(
            Policy::get_fee(state_, params_, pool_config_, research, xp)
        )}});
    }

    json::object update(const json::object& request) {
        const uint256 price_scale(required_string(request, "price_scale"));
        const uint256 price_oracle(required_string(request, "price_oracle"));
        const uint256 last_prices(required_string(request, "last_prices"));
        const std::array<uint256, 2> xp{uint256(0), uint256(0)};
        const uint256 zero(0);
        PolicyResearchContext<uint256> research{};
        research.block_timestamp = now(request);
        const PolicyUpdate<uint256> update{
            xp,
            price_scale,
            price_oracle,
            last_prices,
            zero,
            zero,
            zero,
            research.block_timestamp,
        };
        Policy::update_state(state_, research, params_, pool_config_, update);
        return success({{"state", state_json(state_)}});
    }
};

} // namespace

int main() {
    Evaluator evaluator;
    std::string line;
    while (std::getline(std::cin, line)) {
        json::object response;
        try {
            const json::value parsed = json::parse(line);
            if (!parsed.is_object()) throw std::invalid_argument("request must be an object");
            response = evaluator.handle(parsed.as_object());
        } catch (const std::exception& error) {
            response = {{"ok", false}, {"error", error.what()}};
        }
        std::cout << json::serialize(response) << '\n' << std::flush;
    }
    return 0;
}
