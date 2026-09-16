// engine/combat/include/hue/combat/combat.h
//
// Week 9 gameplay combat state machine plus Week 10 strike/volume tables.
// Pure logic, no allocation: the owner drives it with buffered input,
// per-tick flags (one-shot finished, hit taken), and the anim events fired
// by the runtime (cancel_open/cancel_close, hit_begin/hit_end,
// iframe_begin/iframe_end). Joint-local hitbox/hurtbox defs bind by name;
// overlap happens through the physics facade.
//
// State/cancel rules:
//   locomotion  -> any buffered action starts immediately
//   attack      -> dodge cancels while the cancel window is open (windup
//                  and recovery); attacks chain only in recovery (after
//                  hit_end); anything buffered fires when the clip ends
//   dodge       -> commits to the full roll; i-frames between the iframe
//                  events; buffered actions fire when the clip ends
//   hit react   -> not cancelable; buffered actions fire when it ends
//   death       -> terminal, absorbs everything
//
// Buffered input is "last press wins" and expires after
// Tuning::input_buffer_seconds, so mashing early still comes out the
// moment a cancel window or state end allows it.

#pragma once

#include "hue/core/math.h"

#include <cstdint>

namespace hue::combat {

enum class State : std::uint8_t {
    kLocomotion, // idle/walk/run blend; the machine's rest state
    kAttackLight,
    kAttackHeavy,
    kDodge,
    kHitReact,
    kDeath,
};

enum class Action : std::uint8_t {
    kNone,
    kAttackLight,
    kAttackHeavy,
    kDodge,
};

struct Tuning {
    float input_buffer_seconds = 0.35f;
};

// Health + poise delivered by one successful overlap. Poise at or below
// zero (or a fatal health drop) staggers; otherwise the victim keeps their
// current state and only the health bar moves.
struct Strike {
    float health = 14.0f;
    float poise = 22.0f;
    float hitstop_seconds = 0.055f;
    float shake = 0.10f;
};

inline constexpr Strike kStrikeLight{14.0f, 22.0f, 0.055f, 0.10f};
inline constexpr Strike kStrikeHeavy{28.0f, 45.0f, 0.090f, 0.18f};

struct StrikeContact {
    bool landed = false;
    bool staggered = false;
    bool fatal = false;
    float hitstop_seconds = 0.0f;
    float shake = 0.0f;
};

inline constexpr std::uint32_t kMaxBoundVolumes = 8;
inline constexpr std::uint32_t kMaxJointAliases = 4;

enum class VolumeKind : std::uint8_t { kSphere, kCapsule };

// Joint-local combat volume. Capsules lie on the joint's +Y (KayKit
// weapons bake along handslot.r +Y). Aliases let one def bind on both
// KayKit (`handslot.r`) and the generated tube fixture (`hand_r`).
struct VolumeDef {
    const char* joint_aliases[kMaxJointAliases] = {};
    VolumeKind kind = VolumeKind::kSphere;
    Vec3 local_offset{};
    float radius = 0.2f;
    float half_height = 0.0f;
};

struct BoundVolume {
    std::int32_t joint = -1;
    VolumeKind kind = VolumeKind::kSphere;
    Vec3 local_offset{};
    float radius = 0.2f;
    float half_height = 0.0f;
};

struct BoundVolumeSet {
    BoundVolume items[kMaxBoundVolumes]{};
    std::uint32_t count = 0;
};

inline constexpr VolumeDef kDefaultHitboxes[] = {
    {{"handslot.r", "hand_r", nullptr, nullptr},
     VolumeKind::kCapsule,
     {0.0f, 0.55f, 0.0f},
     0.55f,
     0.50f},
};
inline constexpr std::uint32_t kDefaultHitboxCount =
    sizeof(kDefaultHitboxes) / sizeof(kDefaultHitboxes[0]);

inline constexpr VolumeDef kDefaultHurtboxes[] = {
    {{"head", nullptr, nullptr, nullptr},
     VolumeKind::kSphere,
     {0.0f, 0.15f, 0.08f},
     0.35f,
     0.0f},
    {{"chest", "spine", nullptr, nullptr},
     VolumeKind::kCapsule,
     {0.0f, 0.04f, 0.0f},
     0.40f,
     0.24f},
    {{"hips", nullptr, nullptr, nullptr},
     VolumeKind::kSphere,
     {0.0f, 0.02f, 0.0f},
     0.35f,
     0.0f},
};
inline constexpr std::uint32_t kDefaultHurtboxCount =
    sizeof(kDefaultHurtboxes) / sizeof(kDefaultHurtboxes[0]);

// Resolves the first matching alias per def. Unknown joints are skipped
// so a partial skeleton still binds the volumes it has.
std::uint32_t bind_volumes(const VolumeDef* defs, std::uint32_t def_count,
                           const char* const* joint_names, std::uint32_t joint_count,
                           BoundVolumeSet& out) noexcept;

struct TickFlags {
    bool clip_finished = false; // the one-shot driving the current state ended
    bool hit_taken = false;     // owner detected an incoming hit this tick
    bool fatal = false;         // the hit reduced health to zero
};

[[nodiscard]] const char* state_name(State state) noexcept;

class StateMachine {
public:
    StateMachine() = default;
    explicit StateMachine(Tuning tuning) noexcept : m_tuning(tuning) {}

    // Records a pressed action ("last press wins"). Expires if not consumed
    // within the buffer window.
    void buffer(Action action) noexcept;

    // Forwards one fired animation event by name. Unknown names are ignored
    // so gameplay events (foot_left, ...) can share the same track.
    void handle_event(const char* name) noexcept;

    // Advances one tick and applies transitions. Returns the state after
    // the tick; the owner starts the matching clip when it changed.
    State update(float dt, const TickFlags& flags) noexcept;

    [[nodiscard]] State state() const noexcept { return m_state; }
    // True when the last update() entered a state (also on re-entry into
    // the same state, e.g. a light->light chain): start that state's clip.
    [[nodiscard]] bool entered() const noexcept { return m_entered; }
    [[nodiscard]] float state_seconds() const noexcept { return m_state_seconds; }
    [[nodiscard]] bool invulnerable() const noexcept { return m_invulnerable; }
    [[nodiscard]] bool hitbox_active() const noexcept { return m_hitbox_active; }
    [[nodiscard]] bool cancel_open() const noexcept { return m_cancel_open; }
    [[nodiscard]] Action buffered_action() const noexcept { return m_buffered; }
    [[nodiscard]] bool alive() const noexcept { return m_state != State::kDeath; }

private:
    void enter(State next) noexcept;
    [[nodiscard]] bool attack_state() const noexcept {
        return m_state == State::kAttackLight || m_state == State::kAttackHeavy;
    }
    [[nodiscard]] static State action_state(Action action) noexcept;

    Tuning m_tuning{};
    State m_state = State::kLocomotion;
    float m_state_seconds = 0.0f;
    bool m_entered = false;

    Action m_buffered = Action::kNone;
    float m_buffered_age = 0.0f;

    // Event-driven flags, reset on every state change.
    bool m_cancel_open = false;
    bool m_hit_done = false; // hit_end seen: the attack is in recovery
    bool m_hitbox_active = false;
    bool m_invulnerable = false;
};

} // namespace hue::combat
