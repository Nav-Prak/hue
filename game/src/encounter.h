// game/src/encounter.h
// Game-side fight session for one player and up to seven enemies. Owns
// transient fight state, not immutable assets: actors and controllers live in
// the ActorRoster, which must outlive the session. Every simulation call
// advances exactly one 60 Hz tick; rendering never advances gameplay.
#pragma once

#include "actor_roster.h"
#include "combat_character.h"
#include "follow_camera.h"
#include "hue/combat/ai.h"
#include "hue/core/time.h"
#include "scene_config.h"

inline constexpr hue::Vec3 kEncounterSpawns[2] = {{-0.8f, 0.05f, 3.0f}, {0.8f, 0.05f, 3.0f}};
inline constexpr float kEncounterYaw[2] = {-hue::kPi * 0.5f, hue::kPi * 0.5f};
inline constexpr std::uint32_t kPlayerSlot = 0;

enum class EncounterState { kReady, kFighting, kWon, kLost };
const char* encounter_state_name(EncounterState state) noexcept;

struct FightStats {
    std::uint64_t ticks = 0; // includes hitstop; excludes ready/result screens
    std::uint64_t hitstop_ticks = 0;
    std::uint32_t hits[2]{};     // [0] landed by the player, [1] by all enemies
    std::uint32_t staggers[2]{}; // inflicted by the player / by all enemies
    std::uint32_t dodges = 0;
};

// Things that happened during ticks, for presentation (audio). Bounded: when
// the queue is full further events are dropped, never gameplay.
enum class EncounterEventType : std::uint8_t {
    kFightStart,
    kSwingLight,
    kSwingHeavy,
    kHitLight,
    kHitHeavy,
    kStagger,
    kDodge,
    kDeath,
    kLockOn,
    kLockOff,
    kVictory,
    kDefeat,
};
struct EncounterEvent {
    EncounterEventType type = EncounterEventType::kFightStart;
    std::uint32_t actor = 0;
    hue::Vec3 position{};
};
inline constexpr std::uint32_t kMaxEncounterEvents = 32;

class Encounter {
public:
    Encounter(hue::physics::PhysicsWorld& physics, ActorRoster& roster,
              FollowCamera& camera) noexcept;
    [[nodiscard]] hue::Result<void> reset(hue::LinearArena& arena, hue::MeshDraw* draws);
    void start() noexcept;
    void buffer(hue::combat::Action action) noexcept;
    // Toggle acquires the nearest living enemy in range, or releases.
    void toggle_lock() noexcept;
    // Moves the lock to the next living enemy in range (acquires if unlocked).
    void cycle_lock() noexcept;
    // Candidate changes are validated immediately and only applied at reset.
    [[nodiscard]] bool stage_scene(const SceneConfig& scene) noexcept;
    [[nodiscard]] bool stage_tuning(const GameTuning& tuning) noexcept;
    [[nodiscard]] const GameTuning& tuning() const noexcept { return m_tuning; }
    [[nodiscard]] bool tuning_pending() const noexcept { return m_tuning_pending; }
    [[nodiscard]] const SceneConfig& scene() const noexcept { return m_scene; }
    [[nodiscard]] bool scene_pending() const noexcept { return m_scene_pending; }
    // Incremented each time a scene is applied; presentation re-syncs on change.
    [[nodiscard]] std::uint32_t scene_revision() const noexcept { return m_scene_revision; }

    [[nodiscard]] std::uint32_t actor_count() const noexcept { return m_roster.count(); }
    [[nodiscard]] const CombatCharacter& actor(std::uint32_t slot) const noexcept {
        return m_roster.actor(slot);
    }
    [[nodiscard]] CombatCharacter& actor(std::uint32_t slot) noexcept {
        return m_roster.actor(slot);
    }
    [[nodiscard]] std::uint32_t body(std::uint32_t slot) const noexcept {
        return m_roster.body(slot);
    }
    [[nodiscard]] std::uint32_t enemies_alive() const noexcept;
    // The enemy diagnostics and the HUD describe: the lock target, else the
    // nearest living enemy, else slot 1.
    [[nodiscard]] std::uint32_t focus_enemy() const noexcept;
    [[nodiscard]] const hue::combat::Perception& perception(std::uint32_t slot) const noexcept {
        return m_perception[slot];
    }
    [[nodiscard]] const hue::combat::Perception& perception() const noexcept {
        return m_perception[focus_enemy()];
    }
    [[nodiscard]] const hue::combat::Brain& brain(std::uint32_t slot) const noexcept {
        return m_brains[slot];
    }
    [[nodiscard]] const hue::combat::Brain& brain() const noexcept {
        return m_brains[focus_enemy()];
    }
    [[nodiscard]] hue::combat::AiState ai_state() const noexcept { return brain().state(); }

    [[nodiscard]] hue::Result<void> tick(hue::Vec3 movement, hue::LinearArena& arena,
                                         hue::MeshDraw* draws);
    [[nodiscard]] EncounterState state() const noexcept { return m_state; }
    [[nodiscard]] const FightStats& stats() const noexcept { return m_stats; }
    [[nodiscard]] bool locked() const noexcept { return m_lock_target != 0; }
    [[nodiscard]] std::uint32_t lock_target() const noexcept { return m_lock_target; } // 0 = none
    [[nodiscard]] hue::Vec3 position(std::uint32_t actor) const noexcept;
    [[nodiscard]] float yaw(std::uint32_t actor) const noexcept { return m_yaw[actor]; }
    [[nodiscard]] hue::Mat4 model(std::uint32_t actor) const noexcept;
    [[nodiscard]] float hitstop() const noexcept { return m_hitstop; }
    // Copies and clears queued events; returns how many were written.
    std::uint32_t drain_events(EncounterEvent* out, std::uint32_t capacity) noexcept;

private:
    [[nodiscard]] hue::Result<void> apply_scene(const SceneConfig& scene);
    [[nodiscard]] std::uint32_t nearest_enemy(float max_range) const noexcept;
    void set_lock(std::uint32_t target) noexcept;
    void push_event(EncounterEventType type, std::uint32_t actor) noexcept;

    hue::physics::PhysicsWorld& m_physics;
    ActorRoster& m_roster;
    FollowCamera& m_camera;
    hue::combat::Brain m_brains[kMaxActors]{};
    hue::combat::Perception m_perception[kMaxActors]{};
    EncounterState m_state = EncounterState::kReady;
    FightStats m_stats{};
    SceneConfig m_scene{}, m_next_scene{};
    GameTuning m_tuning{}, m_next_tuning{};
    bool m_tuning_pending = false;
    bool m_scene_pending = false;
    bool m_scene_applied = false;
    std::uint32_t m_scene_revision = 0;
    std::uint32_t m_props[kMaxSceneProps]{};
    std::uint32_t m_prop_count = 0;
    float m_yaw[kMaxActors]{};
    hue::Vec3 m_dodge_direction{};
    hue::combat::State m_last_state[kMaxActors]{};
    // One hit per swing per victim: the enemy swing that last hit the player,
    // and the player swing that last hit each enemy.
    std::uint64_t m_enemy_consumed[kMaxActors]{};
    std::uint64_t m_player_consumed[kMaxActors]{};
    hue::combat::Action m_pending = hue::combat::Action::kNone;
    bool m_toggle_pending = false;
    bool m_cycle_pending = false;
    std::uint32_t m_lock_target = 0;
    float m_hitstop = 0.0f;
    EncounterEvent m_events[kMaxEncounterEvents]{};
    std::uint32_t m_event_count = 0;
};
