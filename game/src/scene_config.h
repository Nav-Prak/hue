// game/src/scene_config.h
// Version 2 scene description: one approved arena, a player plus one to seven
// enemies drawn from approved rigs, static box props, point lights and the sun.
// Everything is fixed-capacity; a scene is validated as a whole before it can
// be staged, and identifiers are whitelisted names, never file paths.
#pragma once
#include "hue/core/math.h"
#include "hue/core/result.h"
#include <cstddef>

inline constexpr unsigned kMaxSceneActors = 8;
inline constexpr unsigned kMaxSceneProps = 16;
inline constexpr unsigned kMaxSceneLights = 4;
inline constexpr float kSceneGroundY = 0.05f; // capsule feet rest just above the floor
inline constexpr float kSceneExtent = 18.0f;  // playable half-width of the 40 m arena

enum class RigId : unsigned char { kKnight, kBarbarian, kCount };
inline constexpr unsigned kRigCount = static_cast<unsigned>(RigId::kCount);
[[nodiscard]] const char* rig_name(RigId rig) noexcept;

struct SceneActor {
    RigId rig = RigId::kKnight;
    hue::Vec3 position{};
    float yaw = 0.0f; // radians
};
struct SceneProp {
    hue::Vec3 position{}; // box center
    hue::Vec3 half_extents{0.5f, 0.5f, 0.5f};
    float yaw = 0.0f;
    hue::Vec3 color{0.6f, 0.6f, 0.6f};
};
struct SceneLight {
    hue::Vec3 position{};
    float radius = 10.0f;
    hue::Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};
struct SceneConfig {
    unsigned actor_count = 2; // actors[0] is the player; the rest are enemies
    SceneActor actors[kMaxSceneActors] = {
        {RigId::kKnight, {-0.8f, kSceneGroundY, 3.0f}, -hue::kPi * 0.5f},
        {RigId::kBarbarian, {0.8f, kSceneGroundY, 3.0f}, hue::kPi * 0.5f},
    };
    unsigned prop_count = 0;
    SceneProp props[kMaxSceneProps]{};
    unsigned light_count = 2;
    SceneLight lights[kMaxSceneLights] = {
        {{0.0f, 4.2f, 0.0f}, 10.0f, {1.0f, 0.55f, 0.25f}, 14.0f},
        {{0.0f, 1.8f, 4.5f}, 8.0f, {0.35f, 0.5f, 1.0f}, 8.0f},
    };
    hue::Vec3 sun_direction{0.4f, -1.0f, 0.3f}; // direction light travels
    hue::Vec3 sun_color{1.0f, 0.96f, 0.9f};
    float sun_intensity = 3.0f;
    hue::Vec3 ambient{0.05f, 0.055f, 0.07f};
    bool shadows = true;
};
[[nodiscard]] bool valid_scene(const SceneConfig& scene) noexcept;
[[nodiscard]] hue::Result<SceneConfig> parse_scene(const void* data, std::size_t size);
// Approved scene names are 1-32 characters of [a-z0-9_]; nothing else can
// reach the file system through a scene request.
[[nodiscard]] bool valid_scene_name(const char* name) noexcept;
