// Pure preview of Twocrypto's policy-target clamp and price_scale actuator.
// The helper is shared by tweak_price and keeper view logic so an off-chain
// trigger cannot drift from the transaction's exact p_new arithmetic.
#pragma once

namespace arb::pools::twocrypto_fx {

template <typename T>
struct PriceScaleActuatorPreview {
    bool policy_target_active{false};
    bool clamped_low{false};
    bool clamped_high{false};
    bool adjustment_step_above_min{false};
    T target_price{T(0)};
    T norm{T(0)};
    T adjustment_step{T(0)};
    T p_new{T(0)};
};

template <typename T>
inline PriceScaleActuatorPreview<T> preview_price_scale_actuator(
    const T& policy_target,
    const T& price_oracle,
    const T& price_scale,
    const T& precision,
    const T& adjustment_step_min,
    const T& adjustment_step_max
) {
    PriceScaleActuatorPreview<T> out{};
    out.policy_target_active = policy_target > T(0);
    out.target_price = price_oracle;
    if (out.policy_target_active) {
        const T policy_bound = price_oracle / T(5);
        const T lo = price_oracle - policy_bound;
        const T hi = price_oracle + policy_bound;
        out.clamped_low = policy_target < lo;
        out.clamped_high = policy_target > hi;
        out.target_price = policy_target < lo
            ? lo
            : (policy_target > hi ? hi : policy_target);
    }

    out.norm = out.target_price * precision / price_scale;
    if (out.norm > precision) {
        out.norm -= precision;
    } else {
        out.norm = precision - out.norm;
    }
    out.adjustment_step = out.norm / T(5);
    if (out.adjustment_step > adjustment_step_max) {
        out.adjustment_step = adjustment_step_max;
    }
    out.adjustment_step_above_min =
        out.adjustment_step > adjustment_step_min;
    out.p_new = price_scale;
    if (out.adjustment_step_above_min) {
        out.p_new = (
            price_scale * (out.norm - out.adjustment_step) +
            out.adjustment_step * out.target_price
        ) / out.norm;
    }
    return out;
}

} // namespace arb::pools::twocrypto_fx
