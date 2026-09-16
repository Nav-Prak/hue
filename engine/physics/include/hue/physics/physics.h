// engine/physics/include/hue/physics/physics.h
//
// Week 8 physics facade over Jolt: a static collision arena, capsule
// character controllers (gravity, slopes, wall sliding), and raycasts for
// the camera probe. Jolt types never cross this boundary; the game and
// tests speak engine math only. All bodies are static this week - dynamic
// rigid bodies arrive with combat props later.

#pragma once

#include "hue/core/math.h"
#include "hue/core/result.h"

#include <cstdint>

namespace hue::physics {

inline constexpr std::uint32_t kMaxPhysicsCharacters = 8;
inline constexpr std::uint32_t kMaxStaticBodies = 128;
inline constexpr std::size_t kMaxCookedVertices = 1u << 20;
inline constexpr std::size_t kMaxCookedIndices = 3u << 20;
inline constexpr float kMaxCombatShapeExtent = 8.0f;

struct RayHit {
    bool hit = false;
    float fraction = 1.0f; // of max_distance
    Vec3 position{};
    Vec3 normal{};
};

struct CharacterDesc {
    Vec3 position{};              // feet position
    float radius = 0.35f;         // capsule radius
    float cylinder_half_height = 0.55f; // capsule = cylinder + hemisphere caps
    float max_slope_degrees = 50.0f;
};

class PhysicsWorld {
public:
    [[nodiscard]] static Result<PhysicsWorld> create() noexcept;

    PhysicsWorld(PhysicsWorld&& other) noexcept;
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(PhysicsWorld&&) = delete;
    ~PhysicsWorld();

    // Static arena geometry; call during load, before the first update.
    [[nodiscard]] Result<void> add_static_box(Vec3 center, Quat rotation,
                                              Vec3 half_extents) noexcept;

    // Cooks a world-space triangle soup (typically baked from imported or
    // procedural render meshes) into one static Jolt mesh body. Indices are
    // triples into positions; out-of-range indices reject the whole mesh.
    [[nodiscard]] Result<void> add_static_mesh(const Vec3* positions,
                                               std::size_t position_count,
                                               const std::uint32_t* indices,
                                               std::size_t index_count) noexcept;

    [[nodiscard]] Result<std::uint32_t> create_character(const CharacterDesc& desc) noexcept;

    // Desired planar velocity for the next update; gravity is engine-managed.
    void set_character_velocity(std::uint32_t character, Vec3 planar_velocity) noexcept;

    // Fixed-tick step: integrates gravity, moves every character with slope
    // and stair handling, and steps the (static) simulation.
    [[nodiscard]] Result<void> update(float delta_seconds) noexcept;

    [[nodiscard]] Vec3 character_position(std::uint32_t character) const noexcept; // feet
    [[nodiscard]] Vec3 character_velocity(std::uint32_t character) const noexcept; // post-solve
    [[nodiscard]] bool character_grounded(std::uint32_t character) const noexcept;

    // Closest-hit raycast against the static arena (direction normalized).
    [[nodiscard]] RayHit cast_ray(Vec3 origin, Vec3 direction,
                                  float max_distance) const noexcept;

    // Week 10 combat queries: Jolt narrow-phase overlap of capsules
    // (Y-axis, COM at the transform) and spheres. Shapes are engine-built
    // from bounded radii, not cooked from asset geometry. Non-finite or
    // oversized arguments miss rather than asserting inside Jolt.
    [[nodiscard]] bool overlap_capsules(const Mat4& a_world, float a_half_height, float a_radius,
                                        const Mat4& b_world, float b_half_height,
                                        float b_radius) const noexcept;
    [[nodiscard]] bool overlap_capsule_sphere(const Mat4& capsule_world, float half_height,
                                              float capsule_radius, Vec3 sphere_center,
                                              float sphere_radius) const noexcept;
    [[nodiscard]] bool overlap_spheres(Vec3 a_center, float a_radius, Vec3 b_center,
                                       float b_radius) const noexcept;

    [[nodiscard]] std::uint32_t static_body_count() const noexcept;
    [[nodiscard]] std::uint32_t character_count() const noexcept;

private:
    struct State;
    explicit PhysicsWorld(State* state) noexcept : m_state(state) {}

    State* m_state = nullptr;
};

} // namespace hue::physics
