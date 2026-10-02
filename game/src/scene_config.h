// game/src/scene_config.h
// Version 1 deliberately configures only the existing arena and two approved rigs.
// Spawn positions are restricted to the arena's obstacle-free south staging strip.
#pragma once
#include "hue/core/math.h"
#include "hue/core/result.h"
#include <cstddef>
struct SceneConfig {
    hue::Vec3 spawns[2] = {{-0.8f, 0.05f, 3.0f}, {0.8f, 0.05f, 3.0f}};
    float yaw[2] = {-hue::kPi * 0.5f, hue::kPi * 0.5f};
};
[[nodiscard]] bool valid_scene(const SceneConfig& scene) noexcept;
[[nodiscard]] hue::Result<SceneConfig> parse_scene(const void* data, std::size_t size);
