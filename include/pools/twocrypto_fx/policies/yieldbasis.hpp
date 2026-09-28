// BTC YB price-scale policy with native pool fees.
#pragma once
#include "dual_ema_price_scale.hpp"

namespace arb::pools::twocrypto_fx {
template <typename T>
struct ChallengeFeePolicy : DualEmaPriceScale<T> {
    using State = typename DualEmaPriceScale<T>::State;
    static constexpr bool USES_NATIVE_FEE = true;
    static T get_fee(
        const State&,
        const PolicyConfig<T>&,
        const PolicyPoolConfig<T>&,
        const PolicyResearchContext<T>&,
        const std::array<T, 2>&
    ) {
        return T(0);
    }

    static T fee_floor(
        const PolicyConfig<T>&,
        const PolicyPoolConfig<T>&,
        const T& native_floor
    ) {
        return native_floor;
    }

};
} // namespace arb::pools::twocrypto_fx
