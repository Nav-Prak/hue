// engine/render/include/hue/render/camera.h
//
// Camera state handed to the renderer each frame. Pure math, no Vulkan:
// whoever owns the camera (fly camera now, follow camera in Week 8) builds
// view/projection and the renderer derives the frustum for culling.

#pragma once

#include "hue/core/math.h"

namespace hue {

struct Camera {
    Mat4 view = Mat4::identity();
    Mat4 projection = Mat4::identity();

    [[nodiscard]] Mat4 view_projection() const noexcept { return projection * view; }
};

} // namespace hue
