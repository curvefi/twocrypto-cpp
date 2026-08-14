#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include <boost/json.hpp>

#include "parity/build_identity.hpp"
#include "parity/json_utils.hpp"
#include "parity/pool_config.hpp"
#include "pools/twocrypto_fx/twocrypto.hpp"

namespace json = boost::json;

namespace {

using arb::pools::twocrypto_fx::PoolTraits;
using arb::pools::twocrypto_fx::TwoCryptoPool;
using arb::pools::twocrypto_fx::uint256;

using arb::parity::env_flag;
using arb::parity::env_u64;
using arb::parity::get_str;
using arb::parity::read_file;

using arb::parity::Action;
using arb::parity::ActionSequence;
using arb::parity::PoolConfig;

// Action amounts are replay data.  Preserve their historical long-double
// materialization boundary while pool/config values use binary64.
template <typename T>
T parse_wad(const std::string& value) {
    if constexpr (std::is_same_v<T, uint256>) {
        return uint256(value);
    } else {
        return static_cast<T>(std::strtold(value.c_str(), nullptr) / 1e18L);
    }
}

template <typename T>
std::string to_int_string(const T& value) {
    if constexpr (std::is_same_v<T, uint256>) {
        return value.template convert_to<std::string>();
    } else {
        return arb::parity::to_int_string(value);
    }
}

template <typename T>
std::string to_wei_string(const T& value) {
    if constexpr (std::is_same_v<T, uint256>) {
        return value.template convert_to<std::string>();
    } else {
        return arb::parity::to_str_1e18(value);
    }
}

template <typename T>
json::object snapshot_pool(const TwoCryptoPool<T>& pool) {
    using Traits = PoolTraits<T>;

    const std::array<T, 2> xp = {
        pool.balances[0] * pool.precisions[0],
        (pool.balances[1] * pool.precisions[1] * pool.cached_price_scale) / Traits::PRECISION(),
    };

    json::object o;
    o["balances"] = json::array{to_wei_string(pool.balances[0]), to_wei_string(pool.balances[1])};
    o["xp"] = json::array{to_wei_string(xp[0]), to_wei_string(xp[1])};
    o["D"] = to_wei_string(pool.D);
    o["virtual_price"] = to_wei_string(pool.get_virtual_price());
    o["xcp_profit"] = to_wei_string(pool.xcp_profit);
    o["lp_xcp_profit"] = to_wei_string(pool.lp_xcp_profit);
    o["admin_balances"] = json::array{
        to_wei_string(pool.admin_balances[0]),
        to_wei_string(pool.admin_balances[1])
    };
    o["price_scale"] = to_wei_string(pool.cached_price_scale);
    o["price_oracle"] = to_wei_string(pool.cached_price_oracle);
    o["last_prices"] = to_wei_string(pool.last_prices);
    o["totalSupply"] = to_wei_string(pool.totalSupply);
    o["timestamp"] = pool.block_timestamp;

    o["donation_shares"] = to_wei_string(pool.donation_shares);
    o["donation_shares_unlocked"] = to_wei_string(pool.donation_unlocked());

    o["donation_protection_expiry_ts"] = to_int_string(pool.donation_protection_expiry_ts);
    o["last_donation_release_ts"] = to_int_string(pool.last_donation_release_ts);

    return o;
}

template <typename T>
void execute_action(TwoCryptoPool<T>& pool, const Action& action) {
    if (!action.is_object) {
        throw std::runtime_error(action.parse_error);
    }
    if (!action.parse_error.empty()) {
        throw std::runtime_error(action.parse_error);
    }
    if (action.type == "exchange") {
        (void)pool.exchange(
            T(action.i), T(action.j), parse_wad<T>(action.dx), PoolTraits<T>::ZERO()
        );
    } else if (action.type == "add_liquidity") {
        const std::array<T, 2> amounts = {
            parse_wad<T>(action.amounts[0]),
            parse_wad<T>(action.amounts[1]),
        };
        (void)pool.add_liquidity(amounts, PoolTraits<T>::ZERO(), action.donation);
    } else if (action.type == "remove_liquidity") {
        std::array<T, 2> min_amounts{PoolTraits<T>::ZERO(), PoolTraits<T>::ZERO()};
        if (action.has_min_amounts) {
            min_amounts = {
                parse_wad<T>(action.min_amounts[0]),
                parse_wad<T>(action.min_amounts[1]),
            };
        }
        (void)pool.remove_liquidity(parse_wad<T>(action.amount), min_amounts);
    } else if (action.type == "time_travel") {
        if (action.has_seconds) {
            if (action.seconds > 0) {
                pool.advance_time(static_cast<uint64_t>(action.seconds));
            }
        } else if (action.has_timestamp) {
            pool.set_block_timestamp(static_cast<uint64_t>(action.timestamp));
        }
    } else {
        throw std::runtime_error("unknown action type: " + action.type);
    }
}


template <typename T>
json::object run_one_pool(
    const PoolConfig<T>& pool_config,
    const ActionSequence& sequence,
    uint64_t snapshot_every
) {
    bool all_success = true;
    bool last_success = true;
    std::string last_error;

    TwoCryptoPool<T> pool = arb::parity::make_pool(pool_config, sequence);

    json::array states;
    json::object last_state;
    bool have_last_state = false;

    if (snapshot_every != 0) {
        states.push_back(snapshot_pool(pool));
    }
    if (!sequence.parse_error.empty()) {
        throw std::runtime_error(sequence.parse_error);
    }

    const auto& actions = sequence.actions;
    for (size_t action_idx = 0; action_idx < actions.size(); ++action_idx) {
        bool success = true;
        std::string err;

        // Vyper/boa reverts are transaction-atomic.  Keep the pool's opaque
        // mutable state separate from its immutable configuration.
        const auto pre_action_state = pool.mutable_snapshot();

        try {
            execute_action(pool, actions[action_idx]);
        } catch (const std::exception& error) {
            success = false;
            err = error.what();
            pool.restore_mutable(pre_action_state);
        }

        all_success = all_success && success;
        last_success = success;
        last_error = err;

        if (snapshot_every != 0) {
            json::object state = snapshot_pool(pool);
            state["action_success"] = success;
            if (!success) {
                state["error"] = err;
            }

            if (snapshot_every == 1) {
                states.push_back(state);
            } else {
                if (((action_idx + 1) % snapshot_every) == 0) {
                    states.push_back(state);
                }
                last_state = state;
                have_last_state = true;
            }
        }
    }

    json::object out;
    out["pool_config"] = pool_config.name;
    out["sequence"] = sequence.name;

    json::object result;
    result["success"] = all_success;

    if (snapshot_every == 0) {
        json::object state = snapshot_pool(pool);
        state["action_success"] = last_success;
        if (!last_success) {
            state["error"] = last_error;
        }
        result["final_state"] = state;
    } else {
        if (snapshot_every > 1 && have_last_state) {
            bool same = false;
            if (!states.empty()) {
                const auto& back = states.back();
                if (back.is_object()) {
                    const auto& back_object = back.as_object();
                    if (
                        back_object.if_contains("timestamp") &&
                        last_state.if_contains("timestamp")
                    ) {
                        same = back_object.at("timestamp") == last_state.at("timestamp");
                    }
                }
            }
            if (!same) {
                states.push_back(last_state);
            }
        }
        result["states"] = states;
    }

    out["result"] = result;
    return out;
}

template <typename T>
int run_harness(
    const std::string& pools_file,
    const std::string& sequences_file,
    const std::string& output_file
) {
    const auto pools_val = json::parse(read_file(pools_file));
    const auto seqs_val = json::parse(read_file(sequences_file));

    const auto& pools_obj = pools_val.as_object();
    const auto& seqs_obj = seqs_val.as_object();
    const auto& pools = pools_obj.at("pools").as_array();
    const auto& seqs = seqs_obj.at("sequences").as_array();
    if (seqs.empty()) {
        throw std::runtime_error("No sequences found");
    }
    const ActionSequence sequence = arb::parity::parse_action_sequence(
        seqs[0].as_object()
    );

    uint64_t snapshot_every = 1;
    if (env_flag("SAVE_LAST_ONLY")) {
        snapshot_every = 0;
    }
    if (const char* value = std::getenv("SNAPSHOT_EVERY")) {
        try {
            const long parsed = std::stol(value);
            snapshot_every = (parsed <= 0) ? 0 : static_cast<uint64_t>(parsed);
        } catch (...) {
        }
    }

    uint64_t threads = std::max<uint64_t>(1, std::thread::hardware_concurrency());
    threads = std::max<uint64_t>(1, env_u64("CPP_THREADS", threads));

    std::atomic<size_t> next{0};
    std::mutex io_mu;
    std::vector<json::object> results(pools.size());

    auto worker = [&]() {
        for (;;) {
            const size_t index = next.fetch_add(1);
            if (index >= pools.size()) return;

            const auto& raw_pool = pools[index];
            std::string name;
            if (raw_pool.is_object()) {
                try {
                    name = get_str(raw_pool.as_object(), "name");
                } catch (...) {
                }
            }

            {
                std::lock_guard<std::mutex> lock(io_mu);
                std::cout << "Processing " << name << "..." << std::endl;
            }

            try {
                if (!raw_pool.is_object()) {
                    throw std::runtime_error("pool config must be an object");
                }
                const PoolConfig<T> config = arb::parity::parse_pool_config<T>(
                    raw_pool.as_object()
                );
                name = config.name;
                results[index] = run_one_pool<T>(config, sequence, snapshot_every);
            } catch (const std::exception& error) {
                json::object out;
                out["pool_config"] = name;
                out["sequence"] = sequence.name;
                json::object result;
                result["success"] = false;
                result["error"] = error.what();
                out["result"] = result;
                results[index] = out;
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(threads));
    for (uint64_t index = 0; index < threads; ++index) {
        workers.emplace_back(worker);
    }
    for (auto& worker_thread : workers) {
        worker_thread.join();
    }

    json::array output_results;
    output_results.reserve(results.size());
    for (auto& result : results) {
        output_results.push_back(result);
    }

    json::object metadata;
    metadata["pool_configs_file"] = pools_file;
    metadata["action_sequences_file"] = sequences_file;
    metadata["total_tests"] = static_cast<uint64_t>(pools.size());

    json::object output;
    output["results"] = output_results;
    output["metadata"] = metadata;

    std::ofstream file(output_file, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot write output file: " + output_file);
    }
    file << json::serialize(output) << "\n";
    return 0;
}

} // namespace

// Numeric mode is selected at compile time via HARNESS_MODE_*; the typed
// binaries (benchmark_harness_{i,d,f,ld}) each define one. Default is double.
#if defined(HARNESS_MODE_I)
using HarnessT = uint256;
#elif defined(HARNESS_MODE_F)
using HarnessT = float;
#elif defined(HARNESS_MODE_LD)
using HarnessT = long double;
#else
using HarnessT = double;
#endif

int main(int argc, char* argv[]) {
    if (arb::parity::handle_build_identity_arg(argc, argv)) {
        return 0;
    }

    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <pools.json> <sequences.json> <output.json>\n";
        return 1;
    }
    return run_harness<HarnessT>(argv[1], argv[2], argv[3]);
}
