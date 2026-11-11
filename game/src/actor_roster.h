// game/src/actor_roster.h
// Owns the combat characters and capsule controllers for up to kMaxActors
// actor slots. Slot 0 is the player; the rest are enemies. Rig assets are
// loaded once and shared by every actor that uses them. Characters and
// controllers are created on first use and then reused: applying a scene
// with fewer actors parks the spare controllers instead of destroying them,
// so repeated scene changes never accumulate bodies or characters.
#pragma once

#include "combat_character.h"
#include "scene_config.h"

#include <cstdint>

inline constexpr std::uint32_t kMaxActors = kMaxSceneActors;
static_assert(kMaxActors <= hue::physics::kMaxPhysicsCharacters);

struct RigAsset {
    hue::asset::SkinnedMeshData data;
    hue::anim::AnimationEventTrack events;
    hue::MeshHandle mesh{};
};

class ActorRoster {
public:
    // `rigs` holds kRigCount loaded rigs, indexed by RigId, that outlive the roster.
    ActorRoster(hue::physics::PhysicsWorld& physics, const RigAsset* const* rigs) noexcept;
    ~ActorRoster();
    ActorRoster(const ActorRoster&) = delete;
    ActorRoster& operator=(const ActorRoster&) = delete;

    // Builds one probe character per rig so tuning can be checked against
    // every rig, including ones no current actor uses. Call once before use.
    [[nodiscard]] hue::Result<void> initialize();
    // Makes slots [0, scene.actor_count) live with the scene's rigs. The
    // active count only changes once every slot is ready.
    [[nodiscard]] hue::Result<void> configure(const SceneConfig& scene);
    [[nodiscard]] bool can_apply_tuning(const GameTuning& tuning) const noexcept;

    [[nodiscard]] std::uint32_t count() const noexcept { return m_count; }
    [[nodiscard]] CombatCharacter& actor(std::uint32_t slot) noexcept { return *m_actors[slot]; }
    [[nodiscard]] const CombatCharacter& actor(std::uint32_t slot) const noexcept {
        return *m_actors[slot];
    }
    [[nodiscard]] std::uint32_t body(std::uint32_t slot) const noexcept { return m_bodies[slot]; }
    [[nodiscard]] RigId rig(std::uint32_t slot) const noexcept { return m_rigs_in_use[slot]; }

private:
    [[nodiscard]] hue::Result<CombatCharacter*> build(RigId rig, std::uint32_t draw_index);
    static void destroy(CombatCharacter*& character) noexcept;

    hue::physics::PhysicsWorld& m_physics;
    const RigAsset* m_rigs[kRigCount]{};
    CombatCharacter* m_probes[kRigCount]{};
    CombatCharacter* m_actors[kMaxActors]{};
    RigId m_rigs_in_use[kMaxActors]{};
    std::uint32_t m_bodies[kMaxActors]{};
    bool m_has_body[kMaxActors]{};
    std::uint32_t m_count = 0;
};
