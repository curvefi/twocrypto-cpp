// Repository-owned compiled policy extension point.
//
// A final executable may select one checked-in policy header with:
//
//   -DTWOCRYPTO_POLICY_HEADER="/abs/path/to/policy.hpp"
//
// That header defines `CompiledPolicy<T>` in this namespace. Targets built
// without a selected header use the native passthrough below. A compiled
// policy may override the fee and price-scale target while the pool retains
// all clamping, step limiting, LP protection, and rollback semantics.
#pragma once

#include "common.hpp"

#ifdef TWOCRYPTO_POLICY_HEADER
#include TWOCRYPTO_POLICY_HEADER
#else
#include "compiled_passthrough.hpp"
#endif

namespace arb {
namespace pools {
namespace twocrypto_fx {

namespace compiled_detail {
#if defined(TWOCRYPTO_POLICY_HEADER)
inline constexpr bool quote_cache_safe = false;
#else
inline constexpr bool quote_cache_safe = true;
#endif
} // namespace compiled_detail

} // namespace twocrypto_fx
} // namespace pools
} // namespace arb
