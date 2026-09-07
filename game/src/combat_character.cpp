// game/src/combat_character.cpp

#include "combat_character.h"

#include "hue/core/log.h"

#include <cstring>

namespace {

// Movement tuning shared with main.cpp: the blend parameter is literally
// the capsule controller's solved ground speed.
constexpr float kWalkSpeed = 1.6f;
constexpr float kRunSpeed = 4.0f;
constexpr float kIdleThreshold = 0.2f;

} // namespace

hue::Result<void> CombatCharacter::initialize() {
    const auto valid = hue::anim::validate_animation_events(m_data, m_events);
    if (!valid) {
        return valid.error();
    }
    struct ClipBinding {
        const char* name;
        std::uint32_t* slot;
    };
    const ClipBinding bindings[] = {
        {"locomotion", &m_run_clip},   {"walk", &m_walk_clip},
        {"idle", &m_idle_clip},        {"attack", &m_attack_clip},
        {"attack_heavy", &m_attack_heavy_clip}, {"dodge", &m_dodge_clip},
        {"hit_react", &m_hit_react_clip},       {"death", &m_death_clip},
    };
    for (const ClipBinding& binding : bindings) {
        const std::int32_t clip = hue::anim::find_clip(m_data, binding.name);
        if (clip < 0) {
            HUE_LOG_ERROR("character is missing required clip '%s'", binding.name);
            return hue::ErrorCode::kCorruptData;
        }
        *binding.slot = static_cast<std::uint32_t>(clip);
    }
    return m_animator.bind(m_data, m_idle_clip);
}

void CombatCharacter::apply_damage(float amount) noexcept {
    if (amount <= 0.0f || m_health <= 0.0f || m_machine.invulnerable() ||
        m_machine.state() == hue::combat::State::kDeath) {
        return;
    }
    m_health -= amount;
    if (m_health < 0.0f) {
        m_health = 0.0f;
    }
    notify_hit(m_health <= 0.0f);
}

hue::Result<void> CombatCharacter::start_state_clip(hue::combat::State state) {
    using hue::combat::State;
    switch (state) {
    case State::kLocomotion:
        // Handled per-tick by the speed blend below; nothing to start here.
        return {};
    case State::kAttackLight:
        return m_animator.play(m_attack_clip, 0.12f, false, true);
    case State::kAttackHeavy:
        return m_animator.play(m_attack_heavy_clip, 0.15f, false, true);
    case State::kDodge:
        return m_animator.play(m_dodge_clip, 0.10f, false, true);
    case State::kHitReact:
        return m_animator.play(m_hit_react_clip, 0.08f, false, true);
    case State::kDeath:
        return m_animator.play(m_death_clip, 0.20f, false, true);
    }
    return hue::ErrorCode::kInvalidArgument;
}

hue::Result<void> CombatCharacter::update(float delta_seconds, hue::LinearArena& arena,
                                          hue::MeshDraw* draws) {
    using hue::combat::State;

    hue::combat::TickFlags flags;
    flags.clip_finished = m_machine.state() != State::kLocomotion && m_animator.finished();
    flags.hit_taken = m_pending_hit;
    flags.fatal = m_pending_fatal;
    m_pending_hit = false;
    m_pending_fatal = false;

    const State state = m_machine.update(delta_seconds, flags);
    if (m_machine.entered()) {
        HUE_LOG_DEBUG("combat state -> %s", hue::combat::state_name(state));
        const auto started = start_state_clip(state);
        if (!started) {
            return started.error();
        }
    }

    if (state == State::kLocomotion) {
        // Week 8 locomotion layer: idle below the threshold, walk/run 1D
        // blend driven by the controller's actual ground speed above it.
        if (m_speed < kIdleThreshold) {
            const auto played = m_animator.play(m_idle_clip, 0.2f, true);
            if (!played) {
                return played.error();
            }
        } else {
            const auto played =
                m_animator.play_blend(m_walk_clip, m_run_clip, kWalkSpeed, kRunSpeed, 0.15f);
            if (!played) {
                return played.error();
            }
            m_animator.set_blend_parameter(m_speed);
        }
    }

    const hue::anim::AnimationEvent* fired[16]{};
    auto frame = m_animator.update(delta_seconds, arena, &m_events, fired, 16);
    if (!frame) {
        return frame.error();
    }
    for (std::uint32_t i = 0; i < frame.value().fired_event_count; ++i) {
        m_machine.handle_event(fired[i]->name);
        HUE_LOG_DEBUG("anim event %s/%.3f: %s (hitbox=%d cancel=%d iframe=%d)", fired[i]->clip,
                      static_cast<double>(fired[i]->time), fired[i]->name,
                      m_machine.hitbox_active() ? 1 : 0, m_machine.cancel_open() ? 1 : 0,
                      m_machine.invulnerable() ? 1 : 0);
    }

    draws[m_draw_index].mesh = m_mesh;
    draws[m_draw_index].joint_matrices = frame.value().skinning_matrices;
    draws[m_draw_index].joint_count = frame.value().joint_count;
    return {};
}
