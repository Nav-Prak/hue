// engine/combat/include/hue/combat/ai.h
//
// Week 11 enemy brain: idle → aggro → approach → attack → recover.
// Pure logic, no allocation and no physics. The owner supplies perception
// (distance, LOS, own combat state) each tick and applies the command
// (planar velocity + optional buffered action) through the same
// CombatCharacter / capsule controller the player uses.

#pragma once

#include "hue/combat/combat.h"
#include "hue/core/math.h"

#include <cstdint>

namespace hue::combat {

enum class AiState : std::uint8_t {
    kIdle,
    kAggro,
    kApproach,
    kAttack,
    kRecover,
};

[[nodiscard]] const char* ai_state_name(AiState state) noexcept;

struct AiTuning {
    float aggro_range = 12.0f;
    float deaggro_range = 18.0f;
    float attack_range = 1.75f;
    float walk_speed = 1.6f;
    float run_speed = 3.2f;
    float strafe_speed = 1.5f;
    float notice_seconds = 0.25f;
    float recover_seconds = 0.65f;
    float los_memory_seconds = 1.5f;
    float light_weight = 0.65f;
    float light_cooldown = 1.8f;
    float heavy_cooldown = 2.4f;
};

struct Perception {
    bool target_alive = false;
    bool self_alive = true;
    bool line_of_sight = false;
    float distance = 0.0f;
    Vec3 to_target{}; // planar player-from-self; length is distance
    State self_state = State::kLocomotion;
};

struct AiCommand {
    Vec3 planar_velocity{};
    Action action = Action::kNone;
    bool face_target = false;
};

class Brain {
public:
    Brain() = default;
    explicit Brain(AiTuning tuning, std::uint32_t seed = 1) noexcept;

    AiCommand update(float dt, const Perception& sense) noexcept;

    [[nodiscard]] AiState state() const noexcept { return m_state; }
    [[nodiscard]] bool entered() const noexcept { return m_entered; }
    [[nodiscard]] Action last_attack() const noexcept { return m_last_attack; }
    [[nodiscard]] float state_seconds() const noexcept { return m_state_seconds; }
    [[nodiscard]] float light_cooldown() const noexcept { return m_light_cd > 0 ? m_light_cd : 0; }
    [[nodiscard]] float heavy_cooldown() const noexcept { return m_heavy_cd > 0 ? m_heavy_cd : 0; }
    [[nodiscard]] float los_lost_seconds() const noexcept { return m_los_lost_seconds; }

private:
    void enter(AiState next) noexcept;
    [[nodiscard]] Action pick_attack() noexcept;
    [[nodiscard]] float next_unit() noexcept;
    [[nodiscard]] Vec3 chase_velocity(const Perception& sense) const noexcept;
    [[nodiscard]] Vec3 strafe_velocity(const Perception& sense) const noexcept;
    [[nodiscard]] bool wants_aggro(const Perception& sense) const noexcept;
    [[nodiscard]] bool keeps_aggro(const Perception& sense) const noexcept;

    AiTuning m_tuning{};
    AiState m_state = AiState::kIdle;
    bool m_entered = false;
    float m_state_seconds = 0.0f;
    float m_los_lost_seconds = 0.0f;
    float m_light_cd = 0.0f;
    float m_heavy_cd = 0.0f;
    Action m_last_attack = Action::kNone;
    float m_strafe_sign = 1.0f;
    std::uint32_t m_rng = 1;
};

} // namespace hue::combat
