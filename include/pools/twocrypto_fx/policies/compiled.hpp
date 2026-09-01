// Repository-owned compiled policy extension point.
//
// A final executable may select one checked-in policy header with:
//
//   -DTWOCRYPTO_POLICY_HEADER="/abs/path/to/policy.hpp"
//
// That header defines the source contract's `ChallengeFeePolicy<T>` in this
// namespace. Targets built without a selected header expose only
// PolicyKind::None. A compiled policy may override the fee and price-scale
// target while the pool retains
// all clamping, step limiting, LP protection, and rollback semantics.
#pragma once

#include <type_traits>

#include "common.hpp"

#ifdef TWOCRYPTO_POLICY_HEADER
#include TWOCRYPTO_POLICY_HEADER
#endif

namespace arb {
namespace pools {
namespace twocrypto_fx {

namespace compiled_detail {

#ifdef TWOCRYPTO_POLICY_HEADER
template <typename Policy, typename = void>
struct UsesNativeFee : std::false_type {};

template <typename Policy>
struct UsesNativeFee<
    Policy,
    std::void_t<decltype(Policy::USES_NATIVE_FEE)>
> : std::bool_constant<Policy::USES_NATIVE_FEE> {};

template <typename T>
inline constexpr bool uses_native_fee_v =
    UsesNativeFee<ChallengeFeePolicy<T>>::value;
#else
template <typename T>
inline constexpr bool uses_native_fee_v = false;
#endif

} // namespace compiled_detail

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
