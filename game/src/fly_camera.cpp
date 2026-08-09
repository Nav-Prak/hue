// game/src/fly_camera.cpp

#include "fly_camera.h"

#include <cmath>

namespace {
constexpr float kLookSensitivity = 0.0025f; // radians per pixel
constexpr float kMoveSpeed = 6.0f;          // meters per second
constexpr float kFastMultiplier = 4.0f;
constexpr float kPitchLimit = 1.55f; // just short of straight up/down
constexpr float kFovYRadians = hue::radians(60.0f);
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 200.0f;
} // namespace

hue::Vec3 FlyCamera::forward() const {
    const float cos_pitch = std::cos(m_pitch);
    return {-std::sin(m_yaw) * cos_pitch, std::sin(m_pitch), -std::cos(m_yaw) * cos_pitch};
}

void FlyCamera::update(const hue::Input& input, float dt) {
    using namespace hue;

    if (input.mouse_down(mouse::kRight)) {
        m_yaw -= static_cast<float>(input.mouse_dx()) * kLookSensitivity;
        m_pitch -= static_cast<float>(input.mouse_dy()) * kLookSensitivity;
        m_pitch = clamp(m_pitch, -kPitchLimit, kPitchLimit);
    }

    const Vec3 view_forward = forward();
    const Vec3 view_right = normalize(cross(view_forward, Vec3{0.0f, 1.0f, 0.0f}));

    Vec3 move{};
    if (input.key_down(key::kW))
        move = move + view_forward;
    if (input.key_down(key::kS))
        move = move - view_forward;
    if (input.key_down(key::kD))
        move = move + view_right;
    if (input.key_down(key::kA))
        move = move - view_right;
    if (input.key_down(key::kE))
        move = move + Vec3{0.0f, 1.0f, 0.0f};
    if (input.key_down(key::kQ))
        move = move - Vec3{0.0f, 1.0f, 0.0f};

    if (length_squared(move) > 0.0f) {
        const float speed =
            input.key_down(key::kLeftShift) ? kMoveSpeed * kFastMultiplier : kMoveSpeed;
        m_position = m_position + normalize(move) * (speed * dt);
    }
}

hue::Camera FlyCamera::camera(float aspect) const {
    hue::Camera out;
    out.view = hue::Mat4::look_at(m_position, m_position + forward(), {0.0f, 1.0f, 0.0f});
    out.projection = hue::Mat4::perspective(kFovYRadians, aspect, kNearPlane, kFarPlane);
    return out;
}
