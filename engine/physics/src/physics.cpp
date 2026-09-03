// engine/physics/src/physics.cpp
//
// Jolt-backed implementation of the physics facade. Jolt allocations are
// routed through the tagged heap (MemoryTag::kPhysics) so they show up in
// the allocation overlay like every other subsystem.

#include "hue/physics/physics.h"

#include "hue/core/log.h"
#include "hue/core/memory.h"
#include "hue/core/trace.h"

#include <cstring>
#include <mutex>
#include <new>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100 4127 4324 4365 4514 4582 4623 4625 4626 4710 4711 4820 5026 5027 5045 5219 5220 5264)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wfloat-equal"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wdeprecated-copy-with-user-provided-dtor"
#endif

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace hue::physics {

namespace {

constexpr float kGravity = -9.81f;

// ---------------------------------------------------------------- layers

namespace layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::ObjectLayer kCount = 2;
} // namespace layers

namespace broad_phase {
constexpr JPH::BroadPhaseLayer kStatic{0};
constexpr JPH::BroadPhaseLayer kMoving{1};
constexpr JPH::uint kCount = 2;
} // namespace broad_phase

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return broad_phase::kCount; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == layers::kStatic ? broad_phase::kStatic : broad_phase::kMoving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == broad_phase::kStatic ? "static" : "moving";
    }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override {
        if (layer == layers::kStatic) {
            return broad == broad_phase::kMoving; // static needs no static pairs
        }
        return true;
    }
};

class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return !(a == layers::kStatic && b == layers::kStatic);
    }
};

// ------------------------------------------------------- global Jolt init

// Jolt's factory/type registry is process-global; reference-count it across
// PhysicsWorld instances (tests create several sequentially).
std::mutex g_init_mutex;
int g_init_count = 0;

void* jolt_allocate(std::size_t size) {
    auto memory = heap_allocate(size, 16, MemoryTag::kPhysics);
    return memory ? memory.value() : nullptr;
}

void* jolt_reallocate(void* block, std::size_t old_size, std::size_t new_size) {
    if (new_size == 0) {
        (void)heap_free(block);
        return nullptr;
    }
    auto memory = heap_allocate(new_size, 16, MemoryTag::kPhysics);
    if (!memory) {
        return nullptr;
    }
    if (block != nullptr) {
        std::memcpy(memory.value(), block, old_size < new_size ? old_size : new_size);
        (void)heap_free(block);
    }
    return memory.value();
}

void jolt_free(void* block) { (void)heap_free(block); }

void* jolt_aligned_allocate(std::size_t size, std::size_t alignment) {
    auto memory = heap_allocate(size, alignment, MemoryTag::kPhysics);
    return memory ? memory.value() : nullptr;
}

#if defined(JPH_ENABLE_ASSERTS)
bool jolt_assert_failed(const char* expression, const char* message, const char* file,
                        JPH::uint line) {
    HUE_LOG_ERROR("jolt assert: %s (%s) at %s:%u", expression,
                  message != nullptr ? message : "", file, line);
    return false; // no breakpoint; the log carries it
}
#endif

[[nodiscard]] Result<void> jolt_global_init() noexcept {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    if (g_init_count == 0) {
        JPH::Allocate = &jolt_allocate;
        JPH::Reallocate = &jolt_reallocate;
        JPH::Free = &jolt_free;
        JPH::AlignedAllocate = &jolt_aligned_allocate;
        JPH::AlignedFree = &jolt_free;
#if defined(JPH_ENABLE_ASSERTS)
        JPH::AssertFailed = &jolt_assert_failed;
#endif
        JPH::Factory::sInstance = new JPH::Factory();
        if (JPH::Factory::sInstance == nullptr) {
            return ErrorCode::kOutOfMemory;
        }
        JPH::RegisterTypes();
    }
    ++g_init_count;
    return {};
}

void jolt_global_shutdown() noexcept {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    if (--g_init_count == 0) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

// ------------------------------------------------------------ conversions

[[nodiscard]] JPH::Vec3 to_jolt(Vec3 v) noexcept { return {v.x, v.y, v.z}; }
[[nodiscard]] JPH::Quat to_jolt(Quat q) noexcept { return {q.x, q.y, q.z, q.w}; }
[[nodiscard]] Vec3 from_jolt(JPH::Vec3Arg v) noexcept {
    return {v.GetX(), v.GetY(), v.GetZ()};
}

struct CharacterSlot {
    JPH::Ref<JPH::CharacterVirtual> character;
    JPH::Vec3 desired_velocity = JPH::Vec3::sZero();
};

} // namespace

// -------------------------------------------------------------------- state

struct PhysicsWorld::State {
    BroadPhaseLayers broad_phase_layers;
    ObjectVsBroadPhaseFilter object_vs_broad_phase;
    ObjectLayerPairFilter object_pair_filter;
    JPH::TempAllocatorImpl temp_allocator{4 * 1024 * 1024};
    JPH::JobSystemSingleThreaded job_system{JPH::cMaxPhysicsJobs};
    JPH::PhysicsSystem system;
    CharacterSlot characters[kMaxPhysicsCharacters];
    std::uint32_t character_count = 0;
    std::uint32_t static_body_count = 0;
};

Result<PhysicsWorld> PhysicsWorld::create() noexcept {
    auto initialized = jolt_global_init();
    if (!initialized) {
        return initialized.error();
    }
    auto memory = heap_allocate(sizeof(State), alignof(State), MemoryTag::kPhysics);
    if (!memory) {
        jolt_global_shutdown();
        return memory.error();
    }
    State* state = new (memory.value()) State();
    state->system.Init(1024, 0, 1024, 256, state->broad_phase_layers,
                       state->object_vs_broad_phase, state->object_pair_filter);
    state->system.SetGravity(JPH::Vec3(0.0f, kGravity, 0.0f));
    HUE_LOG_INFO("physics world ready (Jolt %d.%d.%d)", JPH_VERSION_MAJOR, JPH_VERSION_MINOR,
                 JPH_VERSION_PATCH);
    return PhysicsWorld(state);
}

PhysicsWorld::PhysicsWorld(PhysicsWorld&& other) noexcept : m_state(other.m_state) {
    other.m_state = nullptr;
}

PhysicsWorld::~PhysicsWorld() {
    if (m_state != nullptr) {
        m_state->~State();
        (void)heap_free(m_state);
        jolt_global_shutdown();
    }
}

Result<void> PhysicsWorld::add_static_box(Vec3 center, Quat rotation,
                                          Vec3 half_extents) noexcept {
    if (m_state == nullptr || m_state->static_body_count >= kMaxStaticBodies) {
        return ErrorCode::kInvalidArgument;
    }
    if (half_extents.x <= 0.0f || half_extents.y <= 0.0f || half_extents.z <= 0.0f) {
        return ErrorCode::kInvalidArgument;
    }
    // Jolt requires half extents >= convex radius; clamp the radius for
    // thin boxes instead of rejecting them.
    const float min_extent =
        half_extents.x < half_extents.y
            ? (half_extents.x < half_extents.z ? half_extents.x : half_extents.z)
            : (half_extents.y < half_extents.z ? half_extents.y : half_extents.z);
    const float convex_radius =
        min_extent < JPH::cDefaultConvexRadius ? min_extent * 0.5f : JPH::cDefaultConvexRadius;

    JPH::BoxShapeSettings shape_settings(to_jolt(half_extents), convex_radius);
    auto shape = shape_settings.Create();
    if (shape.HasError()) {
        HUE_LOG_ERROR("static box shape failed: %s", shape.GetError().c_str());
        return ErrorCode::kInvalidArgument;
    }
    JPH::BodyCreationSettings body_settings(shape.Get(), JPH::RVec3(to_jolt(center)),
                                            to_jolt(normalize(rotation)),
                                            JPH::EMotionType::Static, layers::kStatic);
    JPH::BodyInterface& bodies = m_state->system.GetBodyInterface();
    const JPH::BodyID body = bodies.CreateAndAddBody(body_settings, JPH::EActivation::DontActivate);
    if (body.IsInvalid()) {
        return ErrorCode::kOutOfMemory;
    }
    ++m_state->static_body_count;
    return {};
}

Result<void> PhysicsWorld::add_static_mesh(const Vec3* positions, std::size_t position_count,
                                           const std::uint32_t* indices,
                                           std::size_t index_count) noexcept {
    HUE_PROFILE_ZONE("physics::add_static_mesh");
    if (m_state == nullptr || m_state->static_body_count >= kMaxStaticBodies ||
        positions == nullptr || indices == nullptr || position_count == 0 ||
        position_count > kMaxCookedVertices || index_count == 0 ||
        index_count > kMaxCookedIndices || index_count % 3 != 0) {
        return ErrorCode::kInvalidArgument;
    }
    for (std::size_t i = 0; i < index_count; ++i) {
        if (indices[i] >= position_count) {
            return ErrorCode::kCorruptData;
        }
    }

    JPH::VertexList vertices;
    vertices.reserve(position_count);
    for (std::size_t v = 0; v < position_count; ++v) {
        vertices.push_back(JPH::Float3(positions[v].x, positions[v].y, positions[v].z));
    }
    JPH::IndexedTriangleList triangles;
    triangles.reserve(index_count / 3);
    for (std::size_t i = 0; i + 2 < index_count; i += 3) {
        triangles.push_back(JPH::IndexedTriangle(indices[i], indices[i + 1], indices[i + 2]));
    }

    JPH::MeshShapeSettings shape_settings(vertices, triangles);
    auto shape = shape_settings.Create();
    if (shape.HasError()) {
        HUE_LOG_ERROR("static mesh cook failed: %s", shape.GetError().c_str());
        return ErrorCode::kCorruptData;
    }
    JPH::BodyCreationSettings body_settings(shape.Get(), JPH::RVec3::sZero(),
                                            JPH::Quat::sIdentity(), JPH::EMotionType::Static,
                                            layers::kStatic);
    JPH::BodyInterface& bodies = m_state->system.GetBodyInterface();
    const JPH::BodyID body = bodies.CreateAndAddBody(body_settings, JPH::EActivation::DontActivate);
    if (body.IsInvalid()) {
        return ErrorCode::kOutOfMemory;
    }
    ++m_state->static_body_count;
    HUE_LOG_INFO("physics: cooked static mesh (%zu vertices, %zu triangles)", position_count,
                 index_count / 3);
    return {};
}

Result<std::uint32_t> PhysicsWorld::create_character(const CharacterDesc& desc) noexcept {
    if (m_state == nullptr || m_state->character_count >= kMaxPhysicsCharacters ||
        desc.radius <= 0.0f || desc.cylinder_half_height <= 0.0f ||
        desc.max_slope_degrees <= 0.0f || desc.max_slope_degrees >= 90.0f) {
        return ErrorCode::kInvalidArgument;
    }

    JPH::Ref<JPH::Shape> capsule =
        new JPH::CapsuleShape(desc.cylinder_half_height, desc.radius);
    // Offset so the character position is the feet, not the capsule center.
    JPH::Ref<JPH::Shape> shape = new JPH::RotatedTranslatedShape(
        JPH::Vec3(0.0f, desc.cylinder_half_height + desc.radius, 0.0f),
        JPH::Quat::sIdentity(), capsule);

    JPH::CharacterVirtualSettings settings;
    settings.mShape = shape;
    settings.mMaxSlopeAngle = radians(desc.max_slope_degrees);
    settings.mSupportingVolume =
        JPH::Plane(JPH::Vec3::sAxisY(), -desc.radius); // accept ground on the lower cap
    JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
        &settings, JPH::RVec3(to_jolt(desc.position)), JPH::Quat::sIdentity(), 0,
        &m_state->system);

    const std::uint32_t id = m_state->character_count;
    m_state->characters[id].character = character;
    ++m_state->character_count;
    return id;
}

void PhysicsWorld::set_character_velocity(std::uint32_t character,
                                          Vec3 planar_velocity) noexcept {
    if (m_state == nullptr || character >= m_state->character_count) {
        return;
    }
    m_state->characters[character].desired_velocity =
        JPH::Vec3(planar_velocity.x, 0.0f, planar_velocity.z);
}

Result<void> PhysicsWorld::update(float delta_seconds) noexcept {
    HUE_PROFILE_ZONE("physics::update");
    if (m_state == nullptr || delta_seconds <= 0.0f) {
        return ErrorCode::kInvalidArgument;
    }

    for (std::uint32_t i = 0; i < m_state->character_count; ++i) {
        CharacterSlot& slot = m_state->characters[i];
        JPH::CharacterVirtual* character = slot.character.GetPtr();

        const bool grounded =
            character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
        float vertical = character->GetLinearVelocity().GetY();
        vertical = grounded ? 0.0f : vertical + kGravity * delta_seconds;
        character->SetLinearVelocity(slot.desired_velocity + JPH::Vec3(0.0f, vertical, 0.0f));

        JPH::CharacterVirtual::ExtendedUpdateSettings update_settings;
        character->ExtendedUpdate(
            delta_seconds, JPH::Vec3(0.0f, kGravity, 0.0f), update_settings,
            m_state->system.GetDefaultBroadPhaseLayerFilter(layers::kMoving),
            m_state->system.GetDefaultLayerFilter(layers::kMoving), {}, {},
            m_state->temp_allocator);
    }

    const JPH::EPhysicsUpdateError error = m_state->system.Update(
        delta_seconds, 1, &m_state->temp_allocator, &m_state->job_system);
    if (error != JPH::EPhysicsUpdateError::None) {
        HUE_LOG_ERROR("physics update error: %d", static_cast<int>(error));
        return ErrorCode::kOutOfMemory; // only manifold/pair overflow reaches here
    }
    return {};
}

Vec3 PhysicsWorld::character_position(std::uint32_t character) const noexcept {
    if (m_state == nullptr || character >= m_state->character_count) {
        return {};
    }
    return from_jolt(JPH::Vec3(m_state->characters[character].character->GetPosition()));
}

Vec3 PhysicsWorld::character_velocity(std::uint32_t character) const noexcept {
    if (m_state == nullptr || character >= m_state->character_count) {
        return {};
    }
    return from_jolt(m_state->characters[character].character->GetLinearVelocity());
}

bool PhysicsWorld::character_grounded(std::uint32_t character) const noexcept {
    if (m_state == nullptr || character >= m_state->character_count) {
        return false;
    }
    return m_state->characters[character].character->GetGroundState() ==
           JPH::CharacterVirtual::EGroundState::OnGround;
}

RayHit PhysicsWorld::cast_ray(Vec3 origin, Vec3 direction, float max_distance) const noexcept {
    RayHit result;
    if (m_state == nullptr || max_distance <= 0.0f) {
        return result;
    }
    const JPH::RRayCast ray{JPH::RVec3(to_jolt(origin)),
                            to_jolt(direction) * max_distance};
    JPH::RayCastResult hit;
    if (!m_state->system.GetNarrowPhaseQuery().CastRay(
            ray, hit, m_state->system.GetDefaultBroadPhaseLayerFilter(layers::kMoving),
            m_state->system.GetDefaultLayerFilter(layers::kMoving), {})) {
        return result;
    }
    result.hit = true;
    result.fraction = hit.mFraction;
    result.position = from_jolt(JPH::Vec3(ray.GetPointOnRay(hit.mFraction)));

    JPH::BodyLockRead lock(m_state->system.GetBodyLockInterfaceNoLock(), hit.mBodyID);
    if (lock.Succeeded()) {
        result.normal = from_jolt(lock.GetBody().GetWorldSpaceSurfaceNormal(
            hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction)));
    }
    return result;
}

std::uint32_t PhysicsWorld::static_body_count() const noexcept {
    return m_state != nullptr ? m_state->static_body_count : 0;
}

std::uint32_t PhysicsWorld::character_count() const noexcept {
    return m_state != nullptr ? m_state->character_count : 0;
}

} // namespace hue::physics
