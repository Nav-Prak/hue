// game/src/scene_config.cpp
#include "scene_config.h"
#include "config_json.h"
#include <cmath>
#include <cstring>
bool valid_scene(const SceneConfig& scene) noexcept {
    for (unsigned i = 0; i < 2; ++i) {
        const auto p = scene.spawns[i];
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
            !std::isfinite(scene.yaw[i]) || p.x < -3 || p.x > 3 || p.z < 2 || p.z > 4.5f ||
            p.y != 0.05f || std::abs(scene.yaw[i]) > hue::kPi)
            return false;
    }
    return hue::length_squared(scene.spawns[0] - scene.spawns[1]) >= 1.0f;
}
hue::Result<SceneConfig> parse_scene(const void* data, std::size_t size) {
    auto parsed = parse_config_json(data, size);
    if (!parsed || parsed.value().count != 8)
        return hue::ErrorCode::kCorruptData;
    const auto& obj = parsed.value();
    const auto* version = obj.find("version");
    if (!version || version->is_string || version->is_array || version->count != 1 ||
        version->numbers[0] != 1)
        return hue::ErrorCode::kUnsupported;
    const char* keys[] = {"arena", "player", "enemy"};
    const char* ids[] = {"training_arena", "kaykit_knight", "kaykit_barbarian"};
    for (unsigned i = 0; i < 3; ++i) {
        const auto* f = obj.find(keys[i]);
        if (!f || !f->is_string || std::strcmp(f->string, ids[i]))
            return hue::ErrorCode::kCorruptData;
    }
    SceneConfig result;
    const char* positions[] = {"player_spawn", "enemy_spawn"};
    const char* angles[] = {"player_yaw", "enemy_yaw"};
    for (unsigned i = 0; i < 2; ++i) {
        const auto* p = obj.find(positions[i]);
        const auto* a = obj.find(angles[i]);
        if (!p || !p->is_array || p->count != 3 || !a || a->is_string || a->is_array ||
            a->count != 1)
            return hue::ErrorCode::kCorruptData;
        result.spawns[i] = {p->numbers[0], p->numbers[1], p->numbers[2]};
        if (a->numbers[0] < -180 || a->numbers[0] > 180)
            return hue::ErrorCode::kCorruptData;
        result.yaw[i] = hue::radians(a->numbers[0]);
    }
    if (!valid_scene(result))
        return hue::ErrorCode::kCorruptData;
    return result;
}
