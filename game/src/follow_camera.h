// game/src/follow_camera.h
//
// Week 8 third-person follow camera: spring-arm boom behind the player
// with a physics collision probe. Hold right mouse to orbit; the arm
// shortens instantly when geometry intrudes and relaxes back slowly.
// Week 9 lock-on: an optional target eases the yaw toward the line
// player -> target so the enemy stays framed while strafing.

#pragma once

#include "hue/core/input.h"
#include "hue/core/math.h"
#include "hue/physics/physics.h"
#include "hue/render/camera.h"

class FollowCamera {
public:
    void update(const hue::Input& input, float dt, hue::Vec3 target_feet,
                const hue::physics::PhysicsWorld& physics,
                const hue::Vec3* lock_target = nullptr);

    // Combat feel: a world-space spring impulse on the camera (hitstop
    // shake). Damps out through the same critically-damped helper as the pivot.
    void add_impulse(hue::Vec3 velocity) noexcept;

    [[nodiscard]] hue::Camera camera(float aspect) const;
    [[nodiscard]] float yaw() const { return m_yaw; }
    [[nodiscard]] hue::Vec3 position() const { return m_position; }

private:
    [[nodiscard]] hue::Vec3 forward() const;

    float m_yaw = 3.05f;    // radians around +Y; starts looking back at spawn
    float m_pitch = -0.30f; // negative looks down at the character
    float m_arm_length = 4.0f;
    hue::Vec3 m_pivot{-0.8f, 1.5f, 3.0f};
    hue::Vec3 m_pivot_velocity{};
    hue::Vec3 m_position{};
    hue::Vec3 m_shake{};
    hue::Vec3 m_shake_velocity{};
    bool m_pivot_initialized = false;
};
