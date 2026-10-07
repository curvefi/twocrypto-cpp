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
struct HasRepegReserve : std::false_type {};
template <typename Policy, typename T>
struct HasRepegReserve<Policy, T, std::void_t<decltype(Policy::repeg_reserve(
    std::declval<const PolicyConfig<T>&>()))>> : std::true_type {};

template <typename Policy, typename T, typename = void>
struct HasRepegFloor : std::false_type {};
template <typename Policy, typename T>
struct HasRepegFloor<Policy, T, std::void_t<decltype(Policy::repeg_floor(
    std::declval<const typename Policy::State&>(), std::declval<const PolicyConfig<T>&>(),
    std::declval<const T&>()))>> : std::true_type {};

template <typename Policy, typename T, typename = void>
struct HasRepegMinProgress : std::false_type {};
template <typename Policy, typename T>
struct HasRepegMinProgress<Policy, T, std::void_t<decltype(Policy::repeg_min_progress(
    std::declval<const PolicyConfig<T>&>()))>> : std::true_type {};

template <typename Policy, typename T, typename = void>
struct HasLiquidityFee : std::false_type {};
template <typename Policy, typename T>
struct HasLiquidityFee<Policy, T, std::void_t<decltype(Policy::get_liquidity_fee(
    std::declval<const typename Policy::State&>(), std::declval<const PolicyConfig<T>&>(),
    std::declval<const PolicyPoolConfig<T>&>(), std::declval<const PolicyResearchContext<T>&>(),
    std::declval<const std::array<T,2>&>(), std::declval<const T&>()))>> : std::true_type {};

template <typename Policy, typename T, typename = void>
struct HasPreparedFee : std::false_type {};

template <typename Policy, typename T>
struct HasPreparedFee<Policy, T, std::void_t<decltype(Policy::prepare_fee(
    std::declval<const typename Policy::State&>(), std::declval<const PolicyConfig<T>&>(),
    std::declval<const PolicyPoolConfig<T>&>(), std::declval<const PolicyResearchContext<T>&>()))>>
    : std::true_type {};

template <typename Policy, typename T, typename = void>
struct HasLivePoolFee : std::false_type {};

template <typename Policy, typename T>
struct HasLivePoolFee<Policy, T, std::void_t<decltype(Policy::get_fee(
    std::declval<const typename Policy::State&>(), std::declval<const PolicyConfig<T>&>(),
    std::declval<const PolicyPoolConfig<T>&>(), std::declval<const PolicyResearchContext<T>&>(),
    std::declval<const std::array<T, 2>&>(), std::declval<const T&>()))>> : std::true_type {};

template <typename Policy, typename T, typename = void>
struct HasPreparedLivePoolFee : std::false_type {};

template <typename Policy, typename T>
struct HasPreparedLivePoolFee<Policy, T, std::void_t<decltype(Policy::prepare_fee(
    std::declval<const typename Policy::State&>(), std::declval<const PolicyConfig<T>&>(),
    std::declval<const PolicyPoolConfig<T>&>(), std::declval<const PolicyResearchContext<T>&>(),
    std::declval<const T&>()))>> : std::true_type {};

template <typename Policy, typename T, typename = void>
struct HasContextFeeFloor : std::false_type {};

template <typename Policy, typename T>
struct HasContextFeeFloor<Policy, T, std::void_t<decltype(Policy::context_fee_floor(
    std::declval<const typename Policy::State&>(),
    std::declval<const PolicyConfig<T>&>(),
    std::declval<const PolicyPoolConfig<T>&>(),
    std::declval<const PolicyResearchContext<T>&>(), std::size_t{}))>> : std::true_type {};

template <typename Policy, typename T, typename = void>
struct HasReportTerms : std::false_type {};

template <typename Policy, typename T>
struct HasReportTerms<Policy, T, std::void_t<decltype(Policy::report_terms(
    std::declval<const typename Policy::State&>(),
    std::declval<const PolicyConfig<T>&>(),
    std::declval<const PolicyPoolConfig<T>&>(),
    std::declval<const PolicyResearchContext<T>&>(), std::size_t{}))>> : std::true_type {};

template <typename Policy, typename T, typename = void>
struct HasContextMayProfit : std::false_type {};

template <typename Policy, typename T>
struct HasContextMayProfit<Policy, T, std::void_t<decltype(Policy::context_may_profit(
    std::declval<const typename Policy::State&>(),
    std::declval<const PolicyConfig<T>&>(),
    std::declval<const PolicyPoolConfig<T>&>(),
    std::declval<const PolicyResearchContext<T>&>(), std::size_t{},
    std::declval<T>(), std::declval<T>()))>> : std::true_type {};

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
template <typename Policy, typename = void>
struct UsesCachedReports : std::false_type {};

template <typename Policy>
struct UsesCachedReports<Policy, std::void_t<decltype(Policy::USES_CACHED_REPORTS)>>
    : std::bool_constant<Policy::USES_CACHED_REPORTS> {};

template <typename T>
inline constexpr bool uses_cached_reports_v = UsesCachedReports<ChallengeFeePolicy<T>>::value;
#else
template <typename T>
inline constexpr bool uses_native_fee_v = false;
template <typename T>
inline constexpr bool uses_swap_reports_v = false;
template <typename T>
inline constexpr bool uses_cached_reports_v = false;
#endif

} // namespace compiled_detail

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
