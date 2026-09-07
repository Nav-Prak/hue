// engine/combat/include/hue/combat/combat.h
//
// Week 9 gameplay combat state machine. Pure logic, no allocation: the
// owner drives it with buffered input actions, per-tick flags (one-shot
// finished, hit taken), and the anim events fired by the runtime
// (cancel_open/cancel_close, hit_begin/hit_end, iframe_begin/iframe_end).
// The machine answers with the current state; the owner maps states to
// animation clips, movement rules, and (Week 10) hitbox activation.
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
