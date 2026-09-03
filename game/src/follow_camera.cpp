// game/src/follow_camera.cpp

#include "follow_camera.h"

#include "hue/core/spring.h"

#include <cmath>

namespace {
constexpr float kLookSensitivity = 0.0025f; // radians per pixel
constexpr float kPitchMin = -1.15f;         // steep look down
constexpr float kPitchMax = 0.45f;          // shallow look up
constexpr float kDesiredArm = 4.0f;         // meters behind the pivot
constexpr float kMinArm = 0.4f;
constexpr float kProbeMargin = 0.25f;       // keep the near plane off walls
constexpr float kPivotHeight = 1.5f;        // chest height on the rig
constexpr float kPivotOmega = 18.0f;        // pivot spring stiffness
constexpr float kArmInOmega = 40.0f;        // snap in fast when blocked
constexpr float kArmOutOmega = 5.0f;        // relax out slowly
constexpr float kFovYRadians = hue::radians(55.0f);
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 200.0f;
} // namespace

hue::Vec3 FollowCamera::forward() const {
    const float cos_pitch = std::cos(m_pitch);
    return {-std::sin(m_yaw) * cos_pitch, std::sin(m_pitch), -std::cos(m_yaw) * cos_pitch};
}

void FollowCamera::update(const hue::Input& input, float dt, hue::Vec3 target_feet,
                          const hue::physics::PhysicsWorld& physics) {
    using namespace hue;

    if (input.mouse_down(mouse::kRight)) {
        m_yaw -= static_cast<float>(input.mouse_dx()) * kLookSensitivity;
        m_pitch -= static_cast<float>(input.mouse_dy()) * kLookSensitivity;
        m_pitch = clamp(m_pitch, kPitchMin, kPitchMax);
    }

    const Vec3 pivot_target = target_feet + Vec3{0.0f, kPivotHeight, 0.0f};
    if (!m_pivot_initialized) {
        m_pivot = pivot_target;
        m_pivot_velocity = {};
        m_pivot_initialized = true;
    } else {
        spring_damp(m_pivot, m_pivot_velocity, pivot_target, kPivotOmega, dt);
    }

    // Spring-arm probe: cast from the pivot toward the desired camera spot;
    // anything static in the way shortens the boom.
    const Vec3 back = forward() * -1.0f;
    float allowed_arm = kDesiredArm;
    const physics::RayHit hit = physics.cast_ray(m_pivot, back, kDesiredArm + kProbeMargin);
    if (hit.hit) {
        allowed_arm = hit.fraction * (kDesiredArm + kProbeMargin) - kProbeMargin;
        allowed_arm = clamp(allowed_arm, kMinArm, kDesiredArm);
    }
    const float omega = allowed_arm < m_arm_length ? kArmInOmega : kArmOutOmega;
    m_arm_length = damp(m_arm_length, allowed_arm, omega, dt);

    m_position = m_pivot + back * m_arm_length;
}

hue::Camera FollowCamera::camera(float aspect) const {
    hue::Camera out;
    out.view = hue::Mat4::look_at(m_position, m_pivot, {0.0f, 1.0f, 0.0f});
    out.projection = hue::Mat4::perspective(kFovYRadians, aspect, kNearPlane, kFarPlane);
    return out;
}
