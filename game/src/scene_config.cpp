// game/src/scene_config.cpp
#include "scene_config.h"
#include "config_json.h"
#include <cmath>
#include <cstring>

namespace {
constexpr float kActorClearance = 0.45f; // capsule radius plus a little air

bool finite(hue::Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool unit_range(hue::Vec3 v) noexcept {
    return finite(v) && v.x >= 0 && v.x <= 1 && v.y >= 0 && v.y <= 1 && v.z >= 0 && v.z <= 1;
}
// The built-in arena's fixed obstacles, as seen by a standing actor: the
// center pillar, the ring of eight boxes and the ramp. Must track
// build_ground_scene(); a spawn inside one would start the fight stuck.
bool arena_blocks(float x, float z) noexcept {
    const auto inside = [x, z](float cx, float cz, float radius) {
        const float dx = x - cx, dz = z - cz;
        return dx * dx + dz * dz < radius * radius;
    };
    if (inside(0.0f, 0.0f, 1.07f + kActorClearance))
        return true;
    for (int i = 0; i < 8; ++i) {
        const float angle = hue::kTwoPi * static_cast<float>(i) / 8.0f;
        if (inside(6.0f * std::cos(angle), 6.0f * std::sin(angle), 0.71f + kActorClearance))
            return true;
    }
    return x > -5.6f - kActorClearance && x < -2.4f + kActorClearance &&
           z > -1.0f - kActorClearance && z < 1.0f + kActorClearance;
}
bool prop_blocks(const SceneProp& prop, float x, float z) noexcept {
    // Conservative: the prop's bounding circle rather than its rotated box.
    const float radius = std::sqrt(prop.half_extents.x * prop.half_extents.x +
                                   prop.half_extents.z * prop.half_extents.z) +
                         kActorClearance;
    const float dx = x - prop.position.x, dz = z - prop.position.z;
    return dx * dx + dz * dz < radius * radius;
}

bool number(const ConfigField* f, float& out) noexcept {
    if (!f || f->is_string || f->is_array || f->count != 1)
        return false;
    out = f->numbers[0];
    return true;
}
bool vec3(const ConfigField* f, hue::Vec3& out) noexcept {
    if (!f || f->is_string || !f->is_array || f->count != 3)
        return false;
    out = {f->numbers[0], f->numbers[1], f->numbers[2]};
    return true;
}
bool text(const ConfigField* f, const char* expected) noexcept {
    return f && f->is_string && std::strcmp(f->string, expected) == 0;
}
bool degrees(const ConfigField* f, float& radians_out) noexcept {
    float value = 0.0f;
    if (!number(f, value) || value < -180.0f || value > 180.0f)
        return false;
    radians_out = hue::radians(value);
    return true;
}
} // namespace

const char* rig_name(RigId rig) noexcept {
    switch (rig) {
    case RigId::kKnight:
        return "kaykit_knight";
    case RigId::kBarbarian:
        return "kaykit_barbarian";
    case RigId::kCount:
        break;
    }
    return "";
}

bool valid_scene_name(const char* name) noexcept {
    if (!name)
        return false;
    std::size_t length = 0;
    for (; name[length] != '\0'; ++length) {
        const char c = name[length];
        if (length >= 32 || !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return length > 0;
}

bool valid_scene(const SceneConfig& scene) noexcept {
    if (scene.actor_count < 2 || scene.actor_count > kMaxSceneActors ||
        scene.prop_count > kMaxSceneProps || scene.light_count > kMaxSceneLights)
        return false;
    for (unsigned i = 0; i < scene.prop_count; ++i) {
        const auto& p = scene.props[i];
        if (!finite(p.position) || !finite(p.half_extents) || !std::isfinite(p.yaw) ||
            std::abs(p.yaw) > hue::kPi || !unit_range(p.color))
            return false;
        for (const float half : {p.half_extents.x, p.half_extents.y, p.half_extents.z})
            if (half < 0.05f || half > 6.0f)
                return false;
        if (std::abs(p.position.x) > kSceneExtent || std::abs(p.position.z) > kSceneExtent ||
            p.position.y - p.half_extents.y < -0.01f || p.position.y + p.half_extents.y > 12.0f)
            return false;
    }
    for (unsigned i = 0; i < scene.actor_count; ++i) {
        const auto& a = scene.actors[i];
        const auto p = a.position;
        if (static_cast<unsigned>(a.rig) >= kRigCount || !finite(p) || !std::isfinite(a.yaw) ||
            std::abs(a.yaw) > hue::kPi || std::abs(p.x) > kSceneExtent ||
            std::abs(p.z) > kSceneExtent || p.y != kSceneGroundY || arena_blocks(p.x, p.z))
            return false;
        for (unsigned j = 0; j < scene.prop_count; ++j)
            if (prop_blocks(scene.props[j], p.x, p.z))
                return false;
        for (unsigned j = 0; j < i; ++j)
            if (hue::length_squared(p - scene.actors[j].position) < 1.0f)
                return false;
    }
    for (unsigned i = 0; i < scene.light_count; ++i) {
        const auto& l = scene.lights[i];
        if (!finite(l.position) || std::abs(l.position.x) > 40.0f ||
            std::abs(l.position.y) > 40.0f || std::abs(l.position.z) > 40.0f ||
            !std::isfinite(l.radius) || l.radius < 0.1f || l.radius > 60.0f ||
            !unit_range(l.color) || !std::isfinite(l.intensity) || l.intensity < 0.0f ||
            l.intensity > 100.0f)
            return false;
    }
    // The sun must point clearly downward: the shadow projection looks along it.
    const float sun_length = finite(scene.sun_direction) ? hue::length(scene.sun_direction) : 0.0f;
    if (!(sun_length >= 0.1f) || sun_length > 10.0f ||
        scene.sun_direction.y > -0.25f * sun_length || !unit_range(scene.sun_color) ||
        !std::isfinite(scene.sun_intensity) || scene.sun_intensity < 0.0f ||
        scene.sun_intensity > 20.0f || !unit_range(scene.ambient))
        return false;
    return true;
}

hue::Result<SceneConfig> parse_scene(const void* data, std::size_t size) {
    // ~66 KiB of fixed parse storage; static so deep callers and fuzzers do not
    // pay for it on the stack. Scene parsing is main-thread only.
    static ConfigDocument document;
    if (!parse_config_document(data, size, document))
        return hue::ErrorCode::kCorruptData;
    const auto& root = document.root;
    float version = 0.0f;
    if (!number(root.find("version"), version) || version != 2.0f)
        return hue::ErrorCode::kUnsupported;
    const auto* actors = document.list("actors");
    const auto* props = document.list("props");
    const auto* lights = document.list("lights");
    if (root.count != 6 || document.list_count != 3 || !actors || !props || !lights ||
        !text(root.find("arena"), "training_arena"))
        return hue::ErrorCode::kCorruptData;

    SceneConfig scene;
    float shadows = 0.0f;
    const auto* sun_color = root.find("sun_color");
    if (!vec3(root.find("sun_direction"), scene.sun_direction) || !sun_color ||
        sun_color->is_string || !sun_color->is_array || sun_color->count != 4 ||
        !vec3(root.find("ambient"), scene.ambient) || !number(root.find("shadows"), shadows) ||
        (shadows != 0.0f && shadows != 1.0f))
        return hue::ErrorCode::kCorruptData;
    scene.sun_color = {sun_color->numbers[0], sun_color->numbers[1], sun_color->numbers[2]};
    scene.sun_intensity = sun_color->numbers[3];
    scene.shadows = shadows == 1.0f;

    if (actors->count < 2 || actors->count > kMaxSceneActors || props->count > kMaxSceneProps ||
        lights->count > kMaxSceneLights)
        return hue::ErrorCode::kCorruptData;
    scene.actor_count = actors->count;
    for (unsigned i = 0; i < actors->count; ++i) {
        const auto& record = actors->items[i];
        auto& actor = scene.actors[i];
        const auto* rig = record.find("rig");
        bool known = false;
        for (unsigned r = 0; r < kRigCount && rig && rig->is_string; ++r) {
            if (std::strcmp(rig->string, rig_name(static_cast<RigId>(r))) == 0) {
                actor.rig = static_cast<RigId>(r);
                known = true;
            }
        }
        // Exactly one player, and it comes first.
        if (record.count != 4 || !known || !text(record.find("role"), i == 0 ? "player" : "enemy") ||
            !vec3(record.find("position"), actor.position) ||
            !degrees(record.find("yaw"), actor.yaw))
            return hue::ErrorCode::kCorruptData;
    }
    scene.prop_count = props->count;
    for (unsigned i = 0; i < props->count; ++i) {
        const auto& record = props->items[i];
        auto& prop = scene.props[i];
        hue::Vec3 box_size{};
        if (record.count != 5 || !text(record.find("shape"), "box") ||
            !vec3(record.find("position"), prop.position) || !vec3(record.find("size"), box_size) ||
            !degrees(record.find("yaw"), prop.yaw) || !vec3(record.find("color"), prop.color))
            return hue::ErrorCode::kCorruptData;
        prop.half_extents = box_size * 0.5f;
    }
    scene.light_count = lights->count;
    for (unsigned i = 0; i < lights->count; ++i) {
        const auto& record = lights->items[i];
        auto& light = scene.lights[i];
        if (record.count != 4 || !vec3(record.find("position"), light.position) ||
            !number(record.find("radius"), light.radius) ||
            !vec3(record.find("color"), light.color) ||
            !number(record.find("intensity"), light.intensity))
            return hue::ErrorCode::kCorruptData;
    }
    if (!valid_scene(scene))
        return hue::ErrorCode::kCorruptData;
    return scene;
}
