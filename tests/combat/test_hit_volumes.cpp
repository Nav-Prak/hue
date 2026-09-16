#include <doctest/doctest.h>

#include "hue/anim/animation.h"
#include "hue/asset/gltf_loader.h"
#include "hue/combat/combat.h"
#include "hue/core/math.h"
#include "hue/core/memory.h"
#include "hue/physics/physics.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::uint8_t> read_models_file(const char* name) {
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/%s", HUE_TEST_MODELS_DIR, name);
    std::FILE* file = std::fopen(path, "rb");
    REQUIRE_MESSAGE(file != nullptr, path);
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    REQUIRE(size > 0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    REQUIRE(std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size());
    std::fclose(file);
    return bytes;
}

[[nodiscard]] hue::Mat4 model_at(hue::Vec3 position, float yaw_radians) {
    constexpr float kModelYawOffset = hue::kPi;
    return hue::Mat4::trs(position,
                          hue::Quat::from_axis_angle({0.0f, 1.0f, 0.0f},
                                                     yaw_radians + kModelYawOffset),
                          {1.0f, 1.0f, 1.0f});
}

[[nodiscard]] hue::Mat4 volume_world(const hue::Mat4& model, const hue::Mat4* globals,
                                     std::uint32_t joint_count,
                                     const hue::combat::BoundVolume& volume) {
    REQUIRE(globals != nullptr);
    REQUIRE(volume.joint >= 0);
    REQUIRE(static_cast<std::uint32_t>(volume.joint) < joint_count);
    return model * globals[volume.joint] *
           hue::Mat4::trs(volume.local_offset, {}, {1.0f, 1.0f, 1.0f});
}

[[nodiscard]] bool pair_overlaps(hue::physics::PhysicsWorld& physics,
                                 const hue::combat::BoundVolume& hit, const hue::Mat4& hit_world,
                                 const hue::combat::BoundVolume& hurt,
                                 const hue::Mat4& hurt_world) {
    const bool hit_sphere =
        hit.kind == hue::combat::VolumeKind::kSphere || hit.half_height <= 1.0e-4f;
    const bool hurt_sphere =
        hurt.kind == hue::combat::VolumeKind::kSphere || hurt.half_height <= 1.0e-4f;
    const hue::Vec3 hit_center{hit_world.m[12], hit_world.m[13], hit_world.m[14]};
    const hue::Vec3 hurt_center{hurt_world.m[12], hurt_world.m[13], hurt_world.m[14]};
    if (hit_sphere && hurt_sphere) {
        return physics.overlap_spheres(hit_center, hit.radius, hurt_center, hurt.radius);
    }
    if (hurt_sphere) {
        return physics.overlap_capsule_sphere(hit_world, hit.half_height, hit.radius, hurt_center,
                                              hurt.radius);
    }
    if (hit_sphere) {
        return physics.overlap_capsule_sphere(hurt_world, hurt.half_height, hurt.radius,
                                              hit_center, hit.radius);
    }
    return physics.overlap_capsules(hit_world, hit.half_height, hit.radius, hurt_world,
                                    hurt.half_height, hurt.radius);
}

} // namespace

TEST_CASE("combat: KayKit chop overlaps a facing opponent at the opening distance") {
    const char* attackers[] = {"player.glb", "enemy.glb"};
    for (const char* attacker_file : attackers) {
        CAPTURE(attacker_file);
        const auto attacker_bytes = read_models_file(attacker_file);
        const auto defender_bytes = read_models_file("player.glb");
        auto attacker_mesh =
            hue::asset::load_gltf_skinned(attacker_bytes.data(), attacker_bytes.size());
        auto defender_mesh =
            hue::asset::load_gltf_skinned(defender_bytes.data(), defender_bytes.size());
        REQUIRE(attacker_mesh);
        REQUIRE(defender_mesh);

        const auto bind_set = [](const hue::asset::SkinnedMeshData& data,
                                 const hue::combat::VolumeDef* defs, std::uint32_t def_count,
                                 hue::combat::BoundVolumeSet& out) {
            const char* names[hue::asset::kGltfMaxJoints] = {};
            const std::uint32_t count = static_cast<std::uint32_t>(data.joints.size());
            for (std::uint32_t i = 0; i < count; ++i) {
                names[i] = data.joints[i].name;
            }
            REQUIRE(hue::combat::bind_volumes(defs, def_count, names, count, out) >= 1);
        };
        hue::combat::BoundVolumeSet hitboxes;
        hue::combat::BoundVolumeSet hurtboxes;
        bind_set(attacker_mesh.value(), hue::combat::kDefaultHitboxes,
                 hue::combat::kDefaultHitboxCount, hitboxes);
        bind_set(defender_mesh.value(), hue::combat::kDefaultHurtboxes,
                 hue::combat::kDefaultHurtboxCount, hurtboxes);

        const std::int32_t attack = hue::anim::find_clip(attacker_mesh.value(), "attack");
        const std::int32_t idle = hue::anim::find_clip(defender_mesh.value(), "idle");
        REQUIRE(attack >= 0);
        REQUIRE(idle >= 0);

        auto arena = hue::LinearArena::create(256 * 1024, hue::MemoryTag::kAnimation);
        REQUIRE(arena);
        hue::anim::Animator attacker;
        hue::anim::Animator defender;
        REQUIRE(attacker.bind(attacker_mesh.value(), static_cast<std::uint32_t>(attack)));
        REQUIRE(defender.bind(defender_mesh.value(), static_cast<std::uint32_t>(idle)));
        REQUIRE(attacker.play(static_cast<std::uint32_t>(attack), 0.0f, false, true));
        REQUIRE(defender.play(static_cast<std::uint32_t>(idle), 0.0f, true));

        auto physics = hue::physics::PhysicsWorld::create();
        REQUIRE(physics);

        const bool from_right = std::strcmp(attacker_file, "enemy.glb") == 0;
        const hue::Mat4 attacker_model =
            from_right ? model_at({0.8f, 0.0f, 3.0f}, hue::radians(90.0f))
                       : model_at({-0.8f, 0.0f, 3.0f}, hue::radians(-90.0f));
        const hue::Mat4 defender_model =
            from_right ? model_at({-0.8f, 0.0f, 3.0f}, hue::radians(-90.0f))
                       : model_at({0.8f, 0.0f, 3.0f}, hue::radians(90.0f));

        bool overlapped = false;
        float closest_sq = 1.0e9f;
        for (int step = 0; step < 40; ++step) {
            arena.value().reset();
            auto attack_frame = attacker.update(1.0f / 60.0f, arena.value());
            auto idle_frame = defender.update(0.0f, arena.value());
            REQUIRE(attack_frame);
            REQUIRE(idle_frame);
            if (attacker.current_time() < 0.38f) {
                continue;
            }
            if (attacker.current_time() > 0.62f) {
                break;
            }
            for (std::uint32_t i = 0; i < hitboxes.count; ++i) {
                const hue::Mat4 hit_world =
                    volume_world(attacker_model, attack_frame.value().global_matrices,
                                 attack_frame.value().joint_count, hitboxes.items[i]);
                const hue::Vec3 hit_center{hit_world.m[12], hit_world.m[13], hit_world.m[14]};
                for (std::uint32_t j = 0; j < hurtboxes.count; ++j) {
                    const hue::Mat4 hurt_world =
                        volume_world(defender_model, idle_frame.value().global_matrices,
                                     idle_frame.value().joint_count, hurtboxes.items[j]);
                    const hue::Vec3 hurt_center{hurt_world.m[12], hurt_world.m[13],
                                                hurt_world.m[14]};
                    const float dist_sq = length_squared(hit_center - hurt_center);
                    if (dist_sq < closest_sq) {
                        closest_sq = dist_sq;
                    }
                    if (pair_overlaps(physics.value(), hitboxes.items[i], hit_world,
                                      hurtboxes.items[j], hurt_world)) {
                        overlapped = true;
                    }
                }
            }
        }
        INFO("closest volume centers: ", std::sqrt(closest_sq), " m");
        CHECK(overlapped);
    }
}
