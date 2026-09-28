// Swap-local reported fair fee; native price-scale targeting.
#pragma once
#include "reporting_fair_fee_model.hpp"

namespace arb::pools::twocrypto_fx {
template <typename T>
using ChallengeFeePolicy = ReportingFairFeePolicy<T>;
} // namespace arb::pools::twocrypto_fx
