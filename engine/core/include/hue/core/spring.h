// engine/core/include/hue/core/spring.h
//
// Damped-spring helpers (Week 3 spec). Camera follow and combat feel both
// want critically damped motion: fast approach, zero overshoot, framerate
// independent. Uses the exact closed-form step, not Euler integration, so
// it stays stable at any dt.

#pragma once

#include "hue/core/math.h"

namespace hue {

// Convert an approximate half-life (seconds to close ~half the remaining
// distance) into the angular frequency used by the critically damped spring.
[[nodiscard]] inline float halflife_to_omega(float halflife_seconds) noexcept {
    const float kLn2Times2 = 1.3862944f; // envelope e^{-omega t} at half amplitude
    return kLn2Times2 / (halflife_seconds > kEpsilon ? halflife_seconds : kEpsilon);
}

// Exponential damper without velocity state: returns the new value.
// Framerate-independent version of `value += (target - value) * rate`.
[[nodiscard]] inline float damp(float current, float target, float omega, float dt) noexcept {
    return lerp(current, target, 1.0f - std::exp(-omega * dt));
}
[[nodiscard]] inline Vec3 damp(Vec3 current, Vec3 target, float omega, float dt) noexcept {
    return lerp(current, target, 1.0f - std::exp(-omega * dt));
}

// Critically damped spring with velocity state. Exact solution of
// x'' + 2w x' + w^2 (x - target) = 0 over one step:
//   x(t) = target + (j0 + j1 t) e^{-w t},  j0 = x0 - target,  j1 = v0 + w j0
inline void spring_damp(float& position, float& velocity, float target, float omega,
                        float dt) noexcept {
    const float j0 = position - target;
    const float j1 = velocity + omega * j0;
    const float exp_term = std::exp(-omega * dt);
    position = target + (j0 + j1 * dt) * exp_term;
    velocity = (velocity - omega * j1 * dt) * exp_term;
}

inline void spring_damp(Vec3& position, Vec3& velocity, Vec3 target, float omega,
                        float dt) noexcept {
    spring_damp(position.x, velocity.x, target.x, omega, dt);
    spring_damp(position.y, velocity.y, target.y, omega, dt);
    spring_damp(position.z, velocity.z, target.z, omega, dt);
}

} // namespace hue
