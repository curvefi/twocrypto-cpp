#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/json.hpp>

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

uint256 parse_wad(const std::string& value) {
    return uint256(value);
}

std::string to_int_string(const uint256& value) {
    return value.convert_to<std::string>();
}

std::string to_wei_string(const uint256& value) {
    return value.convert_to<std::string>();
}

json::object snapshot_pool(const TwoCryptoPool<uint256>& pool) {
    using Traits = PoolTraits<uint256>;

    const std::array<uint256, 2> xp = {
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
    o["last_admin_fee_claim_timestamp"] = pool.last_admin_fee_claim_timestamp;
    o["price_scale"] = to_wei_string(pool.cached_price_scale);
    o["price_oracle"] = to_wei_string(pool.cached_price_oracle);
    o["last_prices"] = to_wei_string(pool.last_prices);
    o["totalSupply"] = to_wei_string(pool.totalSupply);
    o["caller_lp_balance"] = to_wei_string(pool.single_caller_lp_balance());
    o["timestamp"] = pool.block_timestamp;

    o["donation_shares"] = to_wei_string(pool.donation_shares);
    o["donation_shares_unlocked"] = to_wei_string(pool.donation_unlocked());

    o["donation_protection_expiry_ts"] = to_int_string(pool.donation_protection_expiry_ts);
    o["last_donation_release_ts"] = to_int_string(pool.last_donation_release_ts);

    return o;
}

std::string execute_action(TwoCryptoPool<uint256>& pool, const Action& action) {
    if (!action.is_object) {
        throw std::runtime_error(action.parse_error);
    }
    if (!action.parse_error.empty()) {
        throw std::runtime_error(action.parse_error);
    }
    if (action.type == "exchange") {
        const auto result = pool.exchange(
            uint256(action.i), uint256(action.j), parse_wad(action.dx),
            parse_wad(action.min_dy)
        );
        return to_int_string(result[0]);
    } else if (action.type == "add_liquidity") {
        const std::array<uint256, 2> amounts = {
            parse_wad(action.amounts[0]),
            parse_wad(action.amounts[1]),
        };
        return to_int_string(pool.add_liquidity(
            amounts, parse_wad(action.min_mint_amount), action.donation));
    } else if (action.type == "remove_liquidity") {
        std::array<uint256, 2> min_amounts{PoolTraits<uint256>::ZERO(), PoolTraits<uint256>::ZERO()};
        if (action.has_min_amounts) {
            min_amounts = {
                parse_wad(action.min_amounts[0]),
                parse_wad(action.min_amounts[1]),
            };
        }
        const auto result = pool.remove_liquidity(parse_wad(action.amount), min_amounts);
        return to_int_string(result[0]) + "," + to_int_string(result[1]);
    } else if (action.type == "remove_liquidity_fixed_out") {
        return to_int_string(pool.remove_liquidity_fixed_out(
            parse_wad(action.token_amount),
            static_cast<size_t>(action.i),
            parse_wad(action.amount_i),
            parse_wad(action.min_amount_j)
        ));
    } else if (action.type == "remove_liquidity_one_coin") {
        if (action.i < 0 || action.i > 1) {
            throw std::invalid_argument("coin index out of range");
        }
        return to_int_string(pool.remove_liquidity_fixed_out(
            parse_wad(action.token_amount),
            static_cast<size_t>(1 - action.i),
            PoolTraits<uint256>::ZERO(),
            parse_wad(action.min_amount)
        ));
    } else if (action.type == "get_dy") {
        return to_int_string(pool.get_dy(
            static_cast<size_t>(action.i),
            static_cast<size_t>(action.j),
            parse_wad(action.dx)
        ));
    } else if (action.type == "get_dx") {
        if (action.n_iter < 0) {
            throw std::invalid_argument("n_iter must be nonnegative");
        }
        return to_int_string(pool.get_dx(
            static_cast<size_t>(action.i),
            static_cast<size_t>(action.j),
            parse_wad(action.dy),
            static_cast<size_t>(action.n_iter)
        ));
    } else if (action.type == "time_travel") {
        if (action.has_seconds) {
            if (action.seconds > 0) {
                pool.advance_time(static_cast<uint64_t>(action.seconds));
            }
        } else if (action.has_timestamp) {
            pool.set_block_timestamp(static_cast<uint64_t>(action.timestamp));
        }
        return "";
    } else {
        throw std::runtime_error("unknown action type: " + action.type);
    }
}


json::object run_one_pool(
    const PoolConfig& pool_config,
    const ActionSequence& sequence,
    uint64_t snapshot_every
) {
    bool all_success = true;
    bool last_success = true;
    std::string last_error;
    std::string last_action_result;

    TwoCryptoPool<uint256> pool = arb::parity::make_pool(pool_config, sequence);

    json::array states;

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
        std::string action_result;

        // Vyper/boa reverts are transaction-atomic.  Keep the pool's opaque
        // mutable state separate from its immutable configuration.
        const auto pre_action_state = pool.mutable_snapshot();

        try {
            action_result = execute_action(pool, actions[action_idx]);
        } catch (const std::exception& error) {
            success = false;
            err = error.what();
            pool.restore_mutable(pre_action_state);
        }

        all_success = all_success && success;
        last_success = success;
        last_error = err;
        last_action_result = success ? action_result : "";

        if (snapshot_every != 0) {
            json::object state = snapshot_pool(pool);
            state["action_success"] = success;
            if (success && !action_result.empty()) {
                state["action_result"] = action_result;
            }
            if (!success) {
                state["error"] = err;
            }

            if (snapshot_every == 1) {
                states.push_back(state);
            } else if (
                ((action_idx + 1) % snapshot_every) == 0 ||
                action_idx + 1 == actions.size()
            ) {
                states.push_back(state);
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
        if (last_success && !last_action_result.empty()) {
            state["action_result"] = last_action_result;
        }
        if (!last_success) {
            state["error"] = last_error;
        }
        result["final_state"] = state;
    } else {
        result["states"] = states;
    }

    out["result"] = result;
    return out;
}

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
                const PoolConfig config = arb::parity::parse_pool_config(
                    raw_pool.as_object()
                );
                name = config.name;
                results[index] = run_one_pool(config, sequence, snapshot_every);
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

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <pools.json> <sequences.json> <output.json>\n";
        return 1;
    }
    return run_harness(argv[1], argv[2], argv[3]);
}
