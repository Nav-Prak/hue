// game/src/actor_roster.cpp
#include "actor_roster.h"

#include "hue/core/log.h"
#include "hue/core/memory.h"

#include <new>

ActorRoster::ActorRoster(hue::physics::PhysicsWorld& physics, const RigAsset* const* rigs) noexcept
    : m_physics(physics) {
    for (unsigned rig = 0; rig < kRigCount; ++rig)
        m_rigs[rig] = rigs != nullptr ? rigs[rig] : nullptr;
}

ActorRoster::~ActorRoster() {
    for (auto*& actor : m_actors)
        destroy(actor);
    for (auto*& probe : m_probes)
        destroy(probe);
}

void ActorRoster::destroy(CombatCharacter*& character) noexcept {
    if (character == nullptr)
        return;
    character->~CombatCharacter();
    (void)hue::heap_free(character);
    character = nullptr;
}

hue::Result<CombatCharacter*> ActorRoster::build(RigId rig, std::uint32_t draw_index) {
    if (static_cast<unsigned>(rig) >= kRigCount || m_rigs[static_cast<unsigned>(rig)] == nullptr)
        return hue::ErrorCode::kInvalidArgument;
    const RigAsset& asset = *m_rigs[static_cast<unsigned>(rig)];
    auto memory = hue::heap_allocate(sizeof(CombatCharacter), alignof(CombatCharacter),
                                     hue::MemoryTag::kGame);
    if (!memory)
        return memory.error();
    auto* character =
        ::new (memory.value()) CombatCharacter(asset.data, asset.events, asset.mesh, draw_index);
    const auto ready = character->initialize();
    if (!ready) {
        destroy(character);
        return ready.error();
    }
    return character;
}

hue::Result<void> ActorRoster::initialize() {
    for (unsigned rig = 0; rig < kRigCount; ++rig) {
        if (m_probes[rig] != nullptr)
            continue;
        auto probe = build(static_cast<RigId>(rig), 0);
        if (!probe) {
            HUE_LOG_ERROR("rig %s cannot drive a combat character", rig_name(static_cast<RigId>(rig)));
            return probe.error();
        }
        m_probes[rig] = probe.value();
    }
    return {};
}

bool ActorRoster::can_apply_tuning(const GameTuning& tuning) const noexcept {
    for (const auto* probe : m_probes)
        if (probe == nullptr || !probe->can_apply_tuning(tuning))
            return false;
    return true;
}

hue::Result<void> ActorRoster::configure(const SceneConfig& scene) {
    if (!valid_scene(scene))
        return hue::ErrorCode::kInvalidArgument;
    for (std::uint32_t slot = 0; slot < scene.actor_count; ++slot) {
        const SceneActor& wanted = scene.actors[slot];
        if (!m_has_body[slot]) {
            hue::physics::CharacterDesc capsule;
            capsule.position = wanted.position;
            const auto body = m_physics.create_character(capsule);
            if (!body)
                return body.error();
            m_bodies[slot] = body.value();
            m_has_body[slot] = true;
        }
        if (m_actors[slot] == nullptr || m_rigs_in_use[slot] != wanted.rig) {
            // Build the replacement before retiring the old character.
            auto fresh = build(wanted.rig, slot);
            if (!fresh)
                return fresh.error();
            destroy(m_actors[slot]);
            m_actors[slot] = fresh.value();
            m_rigs_in_use[slot] = wanted.rig;
        }
    }
    for (std::uint32_t slot = 0; slot < kMaxActors; ++slot)
        if (m_has_body[slot])
            m_physics.set_character_enabled(m_bodies[slot], slot < scene.actor_count);
    m_count = scene.actor_count;
    return {};
}
