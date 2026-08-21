// Compile-time metadata for repository-owned pool policies.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace arb {
namespace pools {
namespace twocrypto_fx {

inline constexpr std::uint32_t POLICY_DESCRIPTOR_ABI_VERSION = 1;

struct PolicyParameterDescriptor {
    std::string_view name;
    std::size_t order;
    std::string_view unit;
    long double default_value;
    long double minimum;
    long double maximum;
    long double quantum;
};

template <std::size_t ParameterCount>
struct PolicyDescriptor {
    std::string_view name;
    std::array<PolicyParameterDescriptor, ParameterCount> parameters;

    constexpr std::size_t size() const noexcept {
        return parameters.size();
    }
};

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
