// game/src/encounter.h
// Game-side two-actor session. Owns transient fight state, not immutable assets.
// Actor/controller references must outlive the session. Every simulation call
// advances exactly one 60 Hz tick; rendering never advances gameplay.
#pragma once

#include "combat_character.h"
#include "follow_camera.h"
#include "hue/combat/ai.h"
#include "hue/core/time.h"
#include "scene_config.h"

inline constexpr hue::Vec3 kEncounterSpawns[2] = {{-0.8f, 0.05f, 3.0f}, {0.8f, 0.05f, 3.0f}};
inline constexpr float kEncounterYaw[2] = {-hue::kPi * 0.5f, hue::kPi * 0.5f};

enum class EncounterState { kReady, kFighting, kWon, kLost };
const char* encounter_state_name(EncounterState state) noexcept;

struct FightStats {
    std::uint64_t ticks = 0; // includes hitstop; excludes ready/result screens
    std::uint64_t hitstop_ticks = 0;
    std::uint32_t hits[2]{};
    std::uint32_t staggers[2]{}; // inflicted by each actor
    std::uint32_t dodges = 0;
};

class Encounter {
public:
    Encounter(hue::physics::PhysicsWorld& physics, CombatCharacter& player, CombatCharacter& enemy,
              std::uint32_t player_body, std::uint32_t enemy_body, FollowCamera& camera) noexcept;
    [[nodiscard]] hue::Result<void> reset(hue::LinearArena& arena, hue::MeshDraw* draws);
    void start() noexcept;
    void buffer(hue::combat::Action action) noexcept;
    void toggle_lock() noexcept;
    // Candidate changes are validated immediately and only applied at reset.
    [[nodiscard]] bool stage_scene(const SceneConfig& scene) noexcept;
    [[nodiscard]] const SceneConfig& scene() const noexcept { return m_scene; }
    [[nodiscard]] bool scene_pending() const noexcept { return m_scene_pending; }
    [[nodiscard]] const hue::combat::Perception& perception() const noexcept {
        return m_perception;
    }
    [[nodiscard]] const hue::combat::Brain& brain() const noexcept { return m_brain; }
    [[nodiscard]] hue::Result<void> tick(hue::Vec3 movement, hue::LinearArena& arena,
                                         hue::MeshDraw* draws);
    [[nodiscard]] EncounterState state() const noexcept { return m_state; }
    [[nodiscard]] const FightStats& stats() const noexcept { return m_stats; }
    [[nodiscard]] bool locked() const noexcept { return m_locked; }
    [[nodiscard]] hue::combat::AiState ai_state() const noexcept { return m_brain.state(); }
    [[nodiscard]] hue::Vec3 position(std::uint32_t actor) const noexcept;
    [[nodiscard]] float yaw(std::uint32_t actor) const noexcept { return m_yaw[actor]; }
    [[nodiscard]] hue::Mat4 model(std::uint32_t actor) const noexcept;
    [[nodiscard]] float hitstop() const noexcept { return m_hitstop; }

private:
    hue::physics::PhysicsWorld& m_physics;
    CombatCharacter* m_actors[2];
    std::uint32_t m_bodies[2];
    FollowCamera& m_camera;
    hue::combat::Brain m_brain{{}, 2026u};
    EncounterState m_state = EncounterState::kReady;
    FightStats m_stats{};
    SceneConfig m_scene{}, m_next_scene{};
    bool m_scene_pending = false;
    hue::combat::Perception m_perception{};
    float m_yaw[2] = {kEncounterYaw[0], kEncounterYaw[1]};
    hue::Vec3 m_dodge_direction{};
    hue::combat::State m_previous_state = hue::combat::State::kLocomotion;
    std::uint64_t m_consumed[2]{};
    hue::combat::Action m_pending = hue::combat::Action::kNone;
    bool m_toggle_pending = false;
    bool m_locked = false;
    float m_hitstop = 0.0f;
};
