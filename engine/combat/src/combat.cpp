// engine/combat/src/combat.cpp

#include "hue/combat/combat.h"

#include <cstring>

namespace hue::combat {

const char* state_name(State state) noexcept {
    switch (state) {
    case State::kLocomotion:
        return "locomotion";
    case State::kAttackLight:
        return "attack_light";
    case State::kAttackHeavy:
        return "attack_heavy";
    case State::kDodge:
        return "dodge";
    case State::kHitReact:
        return "hit_react";
    case State::kDeath:
        return "death";
    }
    return "unknown";
}

State StateMachine::action_state(Action action) noexcept {
    switch (action) {
    case Action::kAttackLight:
        return State::kAttackLight;
    case Action::kAttackHeavy:
        return State::kAttackHeavy;
    case Action::kDodge:
        return State::kDodge;
    case Action::kNone:
        break;
    }
    return State::kLocomotion;
}

void StateMachine::buffer(Action action) noexcept {
    if (action == Action::kNone || m_state == State::kDeath) {
        return;
    }
    m_buffered = action;
    m_buffered_age = 0.0f;
}

void StateMachine::handle_event(const char* name) noexcept {
    if (name == nullptr) {
        return;
    }
    if (std::strcmp(name, "cancel_open") == 0) {
        m_cancel_open = true;
    } else if (std::strcmp(name, "cancel_close") == 0) {
        m_cancel_open = false;
    } else if (std::strcmp(name, "hit_begin") == 0) {
        m_hitbox_active = attack_state();
    } else if (std::strcmp(name, "hit_end") == 0) {
        m_hitbox_active = false;
        m_hit_done = true;
    } else if (std::strcmp(name, "iframe_begin") == 0) {
        m_invulnerable = m_state == State::kDodge;
    } else if (std::strcmp(name, "iframe_end") == 0) {
        m_invulnerable = false;
    }
    // Unknown names (foot_left, ...) are gameplay/audio events; ignored here.
}

void StateMachine::enter(State next) noexcept {
    m_state = next;
    m_entered = true;
    m_state_seconds = 0.0f;
    m_hitbox_active = false;
    m_hit_done = false;
    m_invulnerable = false;
    // Attacks start with the windup cancelable (dodge out of a mistimed
    // press); the clip's cancel_close event ends that window.
    m_cancel_open = attack_state();
    if (next == State::kHitReact || next == State::kDeath) {
        m_buffered = Action::kNone; // a stagger throws away stale intent
    }
}

State StateMachine::update(float dt, const TickFlags& flags) noexcept {
    m_entered = false;
    if (m_state == State::kDeath) {
        return m_state;
    }
    m_state_seconds += dt;

    // Age the buffer first so a press held longer than the window cannot
    // fire on the same tick it expires.
    if (m_buffered != Action::kNone) {
        m_buffered_age += dt;
        if (m_buffered_age > m_tuning.input_buffer_seconds) {
            m_buffered = Action::kNone;
        }
    }

    // Damage interrupts everything except active i-frames.
    if (flags.hit_taken && !m_invulnerable) {
        enter(flags.fatal ? State::kDeath : State::kHitReact);
        return m_state;
    }

    const Action buffered = m_buffered;
    switch (m_state) {
    case State::kLocomotion:
        if (buffered != Action::kNone) {
            m_buffered = Action::kNone;
            enter(action_state(buffered));
        }
        break;
    case State::kAttackLight:
    case State::kAttackHeavy: {
        const bool dodge_cancel = buffered == Action::kDodge && m_cancel_open;
        const bool chain_cancel = (buffered == Action::kAttackLight ||
                                   buffered == Action::kAttackHeavy) &&
                                  m_cancel_open && m_hit_done;
        if (dodge_cancel || chain_cancel || (flags.clip_finished && buffered != Action::kNone)) {
            m_buffered = Action::kNone;
            enter(action_state(buffered));
        } else if (flags.clip_finished) {
            enter(State::kLocomotion);
        }
        break;
    }
    case State::kDodge:
    case State::kHitReact:
        if (flags.clip_finished) {
            if (buffered != Action::kNone) {
                m_buffered = Action::kNone;
                enter(action_state(buffered));
            } else {
                enter(State::kLocomotion);
            }
        }
        break;
    case State::kDeath:
        break;
    }
    return m_state;
}

} // namespace hue::combat
