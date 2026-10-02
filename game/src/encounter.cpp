// game/src/encounter.cpp
#include "encounter.h"
#include "hue/core/log.h"
#include "hue/core/spring.h"
#include "hue/core/trace.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kTick = static_cast<float>(hue::FixedTimestep::kTickSeconds);
float wrapped(float angle) noexcept {
    while (angle > hue::kPi)
        angle -= hue::kTwoPi;
    while (angle < -hue::kPi)
        angle += hue::kTwoPi;
    return angle;
}
bool visible(const hue::physics::PhysicsWorld& physics, hue::Vec3 from, hue::Vec3 to) {
    from.y += 1.2f;
    to.y += 1.2f;
    const hue::Vec3 delta = to - from;
    const float distance = length(delta);
    if (distance < 0.05f)
        return true;
    const auto hit = physics.cast_ray(from, delta * (1.0f / distance), distance);
    return !hit.hit || hit.fraction >= 0.92f;
}
} // namespace

const char* encounter_state_name(EncounterState state) noexcept {
    switch (state) {
    case EncounterState::kReady:
        return "READY";
    case EncounterState::kFighting:
        return "FIGHT";
    case EncounterState::kWon:
        return "VICTORY";
    case EncounterState::kLost:
        return "DEFEAT";
    }
    return "UNKNOWN";
}

Encounter::Encounter(hue::physics::PhysicsWorld& physics, CombatCharacter& player,
                     CombatCharacter& enemy, std::uint32_t player_body, std::uint32_t enemy_body,
                     FollowCamera& camera) noexcept
    : m_physics(physics), m_actors{&player, &enemy}, m_bodies{player_body, enemy_body},
      m_camera(camera) {}

hue::Result<void> Encounter::reset(hue::LinearArena& arena, hue::MeshDraw* draws) {
    HUE_PROFILE_ZONE("game::Encounter::reset");
    m_state = EncounterState::kReady;
    if (m_scene_pending) {
        m_scene = m_next_scene;
        m_scene_pending = false;
    }
    m_perception = {};
    m_stats = {};
    m_brain = hue::combat::Brain{{}, 2026u};
    m_pending = hue::combat::Action::kNone;
    m_toggle_pending = m_locked = false;
    m_hitstop = 0.0f;
    m_dodge_direction = {};
    m_previous_state = hue::combat::State::kLocomotion;
    m_camera = FollowCamera{};
    for (std::uint32_t i = 0; i < 2; ++i) {
        m_yaw[i] = m_scene.yaw[i];
        m_consumed[i] = 0;
        auto body = m_physics.reset_character(m_bodies[i], m_scene.spawns[i]);
        if (!body)
            return body.error();
        arena.reset();
        auto actor = m_actors[i]->reset(arena, draws);
        if (!actor)
            return actor.error();
    }
    return {};
}

bool Encounter::stage_scene(const SceneConfig& scene) noexcept {
    if (!valid_scene(scene))
        return false;
    m_next_scene = scene;
    m_scene_pending = true;
    return true;
}

void Encounter::start() noexcept {
    if (m_state == EncounterState::kReady)
        m_state = EncounterState::kFighting;
}
void Encounter::buffer(hue::combat::Action action) noexcept {
    if (m_state == EncounterState::kFighting && action != hue::combat::Action::kNone)
        m_pending = action;
}
void Encounter::toggle_lock() noexcept {
    if (m_state == EncounterState::kFighting)
        m_toggle_pending = !m_toggle_pending;
}
hue::Vec3 Encounter::position(std::uint32_t actor) const noexcept {
    return m_physics.character_position(m_bodies[actor]);
}
hue::Mat4 Encounter::model(std::uint32_t actor) const noexcept {
    return hue::Mat4::trs(position(actor),
                          hue::Quat::from_axis_angle({0.0f, 1.0f, 0.0f}, m_yaw[actor] + hue::kPi),
                          {1.0f, 1.0f, 1.0f});
}

hue::Result<void> Encounter::tick(hue::Vec3 movement, hue::LinearArena& arena,
                                  hue::MeshDraw* draws) {
    HUE_PROFILE_ZONE("game::Encounter::tick");
    using hue::combat::State;
    if (m_state != EncounterState::kFighting) {
        for (auto* actor : m_actors) {
            arena.reset();
            auto pose = actor->present(kTick, arena, draws);
            if (!pose)
                return pose.error();
        }
        return {};
    }
    ++m_stats.ticks;
    if (m_toggle_pending) {
        m_locked = !m_locked && m_actors[1]->alive() && length(position(1) - position(0)) <= 15.0f;
        m_toggle_pending = false;
    }
    if (!m_actors[1]->alive() || length(position(1) - position(0)) > 20.0f)
        m_locked = false;
    if (m_hitstop > 0.0f) {
        m_hitstop = std::max(0.0f, m_hitstop - kTick);
        ++m_stats.hitstop_ticks;
        return {}; // preserve pending action and owned last poses
    }
    m_actors[0]->buffer_action(m_pending);
    m_pending = hue::combat::Action::kNone;
    const hue::Vec3 to_player = position(0) - position(1);
    hue::combat::Perception perception;
    perception.target_alive = m_actors[0]->alive();
    perception.self_alive = m_actors[1]->alive();
    perception.line_of_sight = visible(m_physics, position(1), position(0));
    perception.distance = length(to_player);
    perception.to_target = to_player;
    perception.self_state = m_actors[1]->state();
    m_perception = perception;
    const auto command = m_brain.update(kTick, perception);
    m_actors[1]->buffer_action(command.action);

    if (!std::isfinite(movement.x) || !std::isfinite(movement.z))
        movement = {};
    movement.y = 0.0f;
    hue::Vec3 desired = movement;
    if (m_actors[0]->state() == State::kDodge) {
        const float decay = hue::clamp(1.0f - m_actors[0]->state_seconds() / 0.45f, 0.0f, 1.0f);
        desired = m_dodge_direction * (6.0f * decay);
    } else if (m_actors[0]->state() != State::kLocomotion) {
        desired = {};
    }
    m_physics.set_character_velocity(m_bodies[0], desired);
    m_physics.set_character_velocity(m_bodies[1], command.planar_velocity);
    auto step = m_physics.update(kTick);
    if (!step)
        return step.error();
    for (std::uint32_t i = 0; i < 2; ++i) {
        const auto velocity = m_physics.character_velocity(m_bodies[i]);
        const hue::Vec3 planar{velocity.x, 0.0f, velocity.z};
        hue::Vec3 face = planar;
        if ((i == 0 && m_locked) || (i == 1 && command.face_target))
            face = position(1u - i) - position(i);
        if (length_squared(face) > 0.04f) {
            const float target = std::atan2(-face.x, -face.z);
            m_yaw[i] += hue::damp(0.0f, wrapped(target - m_yaw[i]), 14.0f, kTick);
        }
        m_actors[i]->set_locomotion_speed(length(planar));
        arena.reset();
        auto pose = m_actors[i]->update(kTick, arena, draws);
        if (!pose)
            return pose.error();
    }
    if (m_actors[0]->state() == State::kDodge && m_previous_state != State::kDodge) {
        ++m_stats.dodges;
        m_dodge_direction = length_squared(movement) > 0.01f
                                ? normalize(movement)
                                : hue::Vec3{std::sin(m_yaw[0]), 0.0f, std::cos(m_yaw[0])};
    }
    m_previous_state = m_actors[0]->state();

    // Snapshot BOTH contacts before applying either. Simultaneous lethal hits
    // trade, with player death resolving to defeat, independent of iteration order.
    bool contacts[2]{};
    for (std::uint32_t i = 0; i < 2; ++i) {
        auto& attacker = *m_actors[i];
        auto& defender = *m_actors[1u - i];
        contacts[i] = attacker.alive() && defender.alive() && !defender.invulnerable() &&
                      attacker.strike_window() && attacker.activation() != m_consumed[i] &&
                      attacker.hitbox_overlaps(model(i), defender, model(1u - i), m_physics);
    }
    for (std::uint32_t i = 0; i < 2; ++i) {
        if (!contacts[i])
            continue;
        const auto strike = m_actors[i]->state() == State::kAttackHeavy ? hue::combat::kStrikeHeavy
                                                                        : hue::combat::kStrikeLight;
        const auto contact = m_actors[1u - i]->apply_strike(strike);
        if (!contact.landed)
            continue;
        m_consumed[i] = m_actors[i]->activation();
        ++m_stats.hits[i];
        if (contact.staggered)
            ++m_stats.staggers[i];
        m_hitstop = std::max(m_hitstop, contact.hitstop_seconds);
        hue::Vec3 direction = position(1u - i) - position(i);
        direction.y = 0.25f;
        m_camera.add_impulse(normalize(direction) * (contact.shake * 6.0f));
    }
    if (!m_actors[0]->alive() || !m_actors[1]->alive()) {
        m_state = !m_actors[0]->alive() ? EncounterState::kLost : EncounterState::kWon;
        m_locked = false;
        m_pending = hue::combat::Action::kNone;
        m_toggle_pending = false;
        m_hitstop = 0.0f;
        for (auto body : m_bodies)
            m_physics.set_character_velocity(body, {});
        HUE_LOG_INFO("fight_summary outcome=%s seed=2026 ticks=%llu hitstop_ticks=%llu "
                     "hits_landed=%u hits_taken=%u staggers=%u dodges=%u",
                     encounter_state_name(m_state), static_cast<unsigned long long>(m_stats.ticks),
                     static_cast<unsigned long long>(m_stats.hitstop_ticks), m_stats.hits[0],
                     m_stats.hits[1], m_stats.staggers[0], m_stats.dodges);
    }
    return {};
}
