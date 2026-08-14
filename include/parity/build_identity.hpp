#pragma once

#include <iostream>
#include <string_view>

#include <boost/json.hpp>

#ifndef TWOCRYPTO_POOL_REVISION
#define TWOCRYPTO_POOL_REVISION "unknown"
#endif
#ifndef TWOCRYPTO_POOL_BUILD_MODE
#define TWOCRYPTO_POOL_BUILD_MODE "default"
#endif
#ifndef TWOCRYPTO_POOL_COMPILER
#define TWOCRYPTO_POOL_COMPILER "unknown"
#endif
#ifndef TWOCRYPTO_POOL_TARGET
#define TWOCRYPTO_POOL_TARGET "unknown"
#endif
#ifndef TWOCRYPTO_POOL_POLICY_PATH
#define TWOCRYPTO_POOL_POLICY_PATH ""
#endif
#ifndef TWOCRYPTO_POOL_POLICY_SHA256
#define TWOCRYPTO_POOL_POLICY_SHA256 ""
#endif

namespace arb::parity {

inline bool handle_build_identity_arg(int argc, char* argv[]) {
    if (argc != 2 || argv == nullptr || argv[1] == nullptr) {
        return false;
    }

    const std::string_view arg(argv[1]);
    if (arg == "--version") {
        std::cout << "twocrypto-pool revision=" << TWOCRYPTO_POOL_REVISION
                  << " build_mode=" << TWOCRYPTO_POOL_BUILD_MODE
                  << " compiler=" << TWOCRYPTO_POOL_COMPILER << "\n";
        return true;
    }
    if (arg == "--identity-json") {
        boost::json::object identity;
        identity["schema_version"] = "twocrypto_pool_identity_v1";
        identity["target"] = TWOCRYPTO_POOL_TARGET;
        identity["pool_revision"] = TWOCRYPTO_POOL_REVISION;
        identity["build_mode"] = TWOCRYPTO_POOL_BUILD_MODE;
        identity["compiler"] = TWOCRYPTO_POOL_COMPILER;
        identity["policy_path"] = TWOCRYPTO_POOL_POLICY_PATH;
        identity["policy_sha256"] = TWOCRYPTO_POOL_POLICY_SHA256;
        std::cout << boost::json::serialize(identity) << '\n';
        return true;
    }
    return false;
}

} // namespace arb::parity
