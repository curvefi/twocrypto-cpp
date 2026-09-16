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
#include <utility>

#include "pools/twocrypto_fx/policy_types.hpp"

#ifdef TWOCRYPTO_POLICY_HEADER
#include TWOCRYPTO_POLICY_HEADER
#endif

namespace arb {
namespace pools {
namespace twocrypto_fx {

namespace compiled_detail {

#ifdef TWOCRYPTO_POLICY_HEADER
template <typename Policy, typename T, typename = void>
struct HasContextFeeFloor : std::false_type {};

template <typename Policy, typename T>
struct HasContextFeeFloor<Policy, T, std::void_t<decltype(Policy::context_fee_floor(
    std::declval<const typename Policy::State&>(),
    std::declval<const PolicyConfig<T>&>(),
    std::declval<const PolicyPoolConfig<T>&>(),
    std::declval<const PolicyResearchContext<T>&>(), std::size_t{}))>> : std::true_type {};

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
template <typename Policy, typename = void>
struct UsesSwapReports : std::false_type {};

template <typename Policy>
struct UsesSwapReports<Policy, std::void_t<decltype(Policy::USES_SWAP_REPORTS)>>
    : std::bool_constant<Policy::USES_SWAP_REPORTS> {};

template <typename T>
inline constexpr bool uses_swap_reports_v = UsesSwapReports<ChallengeFeePolicy<T>>::value;
#else
template <typename T>
inline constexpr bool uses_native_fee_v = false;
template <typename T>
inline constexpr bool uses_swap_reports_v = false;
#endif

} // namespace compiled_detail

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
