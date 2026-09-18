// engine/combat/src/ai.cpp

#include "hue/combat/ai.h"

#include <cmath>

namespace hue::combat {

namespace {

[[nodiscard]] Vec3 planar(Vec3 v) noexcept { return {v.x, 0.0f, v.z}; }

} // namespace

const char* ai_state_name(AiState state) noexcept {
    switch (state) {
    case AiState::kIdle:
        return "idle";
    case AiState::kAggro:
        return "aggro";
    case AiState::kApproach:
        return "approach";
    case AiState::kAttack:
        return "attack";
    case AiState::kRecover:
        return "recover";
    }
    return "unknown";
}

Brain::Brain(AiTuning tuning, std::uint32_t seed) noexcept
    : m_tuning(tuning), m_rng(seed == 0 ? 1u : seed) {}

void Brain::enter(AiState next) noexcept {
    m_state = next;
    m_entered = true;
    m_state_seconds = 0.0f;
    if (next == AiState::kRecover) {
        m_strafe_sign = next_unit() < 0.5f ? -1.0f : 1.0f;
    }
}

float Brain::next_unit() noexcept {
    m_rng = m_rng * 1664525u + 1013904223u;
    return static_cast<float>((m_rng >> 8) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

bool Brain::wants_aggro(const Perception& sense) const noexcept {
    return sense.target_alive && sense.line_of_sight &&
           sense.distance <= m_tuning.aggro_range;
}

bool Brain::keeps_aggro(const Perception& sense) const noexcept {
    if (!sense.target_alive) {
        return false;
    }
    if (sense.distance > m_tuning.deaggro_range) {
        return false;
    }
    return sense.line_of_sight || m_los_lost_seconds < m_tuning.los_memory_seconds;
}

Vec3 Brain::chase_velocity(const Perception& sense) const noexcept {
    const Vec3 dir = planar(sense.to_target);
    const float dist_sq = length_squared(dir);
    if (dist_sq <= m_tuning.attack_range * m_tuning.attack_range) {
        return {};
    }
    const float speed = sense.distance > 4.0f ? m_tuning.run_speed : m_tuning.walk_speed;
    return normalize(dir) * speed;
}

Vec3 Brain::strafe_velocity(const Perception& sense) const noexcept {
    const Vec3 dir = planar(sense.to_target);
    if (length_squared(dir) < 0.01f) {
        return {};
    }
    const Vec3 side = normalize(cross({0.0f, 1.0f, 0.0f}, dir));
    return side * (m_tuning.strafe_speed * m_strafe_sign);
}

Action Brain::pick_attack() noexcept {
    const bool light_ready = m_light_cd <= 0.0f;
    const bool heavy_ready = m_heavy_cd <= 0.0f;
    if (!light_ready && !heavy_ready) {
        return Action::kNone;
    }
    Action chosen = Action::kAttackLight;
    if (light_ready && heavy_ready) {
        chosen = next_unit() < m_tuning.light_weight ? Action::kAttackLight
                                                     : Action::kAttackHeavy;
    } else if (heavy_ready) {
        chosen = Action::kAttackHeavy;
    }
    if (chosen == Action::kAttackHeavy) {
        m_heavy_cd = m_tuning.heavy_cooldown;
    } else {
        m_light_cd = m_tuning.light_cooldown;
    }
    m_last_attack = chosen;
    return chosen;
}

AiCommand Brain::update(float dt, const Perception& sense) noexcept {
    m_entered = false;
    if (!(dt > 0.0f) || !std::isfinite(dt)) {
        return {};
    }
    m_state_seconds += dt;
    if (m_light_cd > 0.0f) {
        m_light_cd -= dt;
    }
    if (m_heavy_cd > 0.0f) {
        m_heavy_cd -= dt;
    }

    if (sense.line_of_sight) {
        m_los_lost_seconds = 0.0f;
    } else {
        m_los_lost_seconds += dt;
    }

    if (!sense.self_alive || !sense.target_alive ||
        (m_state != AiState::kIdle && !keeps_aggro(sense))) {
        if (m_state != AiState::kIdle) {
            enter(AiState::kIdle);
        }
        return {};
    }

    if (m_state == AiState::kIdle) {
        if (wants_aggro(sense)) {
            enter(AiState::kAggro);
        } else {
            return {};
        }
    }

    // Stagger/death roots the body; keep facing but don't pick a new attack.
    if (sense.self_state == State::kHitReact || sense.self_state == State::kDeath) {
        AiCommand stunned;
        stunned.face_target = m_state != AiState::kIdle;
        return stunned;
    }

    AiCommand command;
    command.face_target = true;

    switch (m_state) {
    case AiState::kIdle:
        command.face_target = false;
        break;
    case AiState::kAggro:
        if (m_state_seconds >= m_tuning.notice_seconds) {
            enter(AiState::kApproach);
        }
        break;
    case AiState::kApproach:
        if (sense.distance <= m_tuning.attack_range &&
            sense.self_state == State::kLocomotion) {
            if (sense.line_of_sight) {
                const Action attack = pick_attack();
                if (attack != Action::kNone) {
                    command.action = attack;
                    enter(AiState::kAttack);
                    break;
                }
            }
            command.planar_velocity = strafe_velocity(sense);
        } else {
            command.planar_velocity = chase_velocity(sense);
        }
        break;
    case AiState::kAttack:
        if (sense.self_state == State::kLocomotion &&
            m_state_seconds > 0.05f) {
            // Combat consumed the swing and returned to locomotion (or the
            // buffer never took, in which case we recycle through recover).
            enter(AiState::kRecover);
            command.planar_velocity = strafe_velocity(sense);
        }
        break;
    case AiState::kRecover:
        command.planar_velocity = strafe_velocity(sense);
        if (m_state_seconds >= m_tuning.recover_seconds) {
            enter(AiState::kApproach);
        }
        break;
    }
    return command;
}

} // namespace hue::combat
