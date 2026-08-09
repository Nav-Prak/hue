// game/src/fly_camera.h
//
// Debug fly camera (Week 5 spec). Hold right mouse to look; WASD moves
// along the view direction, Q/E moves down/up, shift is fast. Replaced by
// the third-person follow camera in Week 8; this stays as the debug cam.

#pragma once

#include "hue/core/input.h"
#include "hue/core/math.h"
#include "hue/render/camera.h"

class FlyCamera {
public:
    void update(const hue::Input& input, float dt);

    [[nodiscard]] hue::Camera camera(float aspect) const;
    [[nodiscard]] hue::Vec3 position() const { return m_position; }

private:
    [[nodiscard]] hue::Vec3 forward() const;

    hue::Vec3 m_position{0.0f, 2.0f, 6.0f};
    float m_yaw = 0.0f;     // radians around +Y; 0 looks down -Z
    float m_pitch = -0.25f; // radians; positive looks up
};
