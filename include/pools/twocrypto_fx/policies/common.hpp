// Common helpers for twocrypto policy implementations.
#pragma once

#include "../policy_types.hpp"

namespace arb {
namespace pools {
namespace twocrypto_fx {

template <typename T>
inline T clamp_policy_value(const T& value, const T& lower, const T& upper) {
    if (value < lower) {
        return lower;
    }
    if (value > upper) {
        return upper;
    }
    return value;
}

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
