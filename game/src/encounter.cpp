// game/src/encounter.cpp
#include "encounter.h"
#include "hue/core/log.h"
#include "hue/core/spring.h"
#include "hue/core/trace.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kTick = static_cast<float>(hue::FixedTimestep::kTickSeconds);
constexpr float kEnemySpacing = 1.1f;      // enemies closer than this steer apart
constexpr float kEnemySeparationSpeed = 1.5f;
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

Encounter::Encounter(hue::physics::PhysicsWorld& physics, ActorRoster& roster,
                     FollowCamera& camera) noexcept
    : m_physics(physics), m_roster(roster), m_camera(camera) {}

hue::Result<void> Encounter::apply_scene(const SceneConfig& scene) {
    const auto configured = m_roster.configure(scene);
    if (!configured)
        return configured.error();
    // Build the new props before retiring the old ones so a failure can back
    // out without leaving the arena half-changed.
    std::uint32_t fresh[kMaxSceneProps]{};
    std::uint32_t built = 0;
    for (; built < scene.prop_count; ++built) {
        const auto& prop = scene.props[built];
        const auto body = m_physics.create_prop_box(
            prop.position, hue::Quat::from_axis_angle({0.0f, 1.0f, 0.0f}, prop.yaw),
            prop.half_extents);
        if (!body) {
            for (std::uint32_t i = 0; i < built; ++i)
                m_physics.destroy_prop(fresh[i]);
            if (m_scene_applied)
                (void)m_roster.configure(m_scene);
            return body.error();
        }
        fresh[built] = body.value();
    }
    for (std::uint32_t i = 0; i < m_prop_count; ++i)
        m_physics.destroy_prop(m_props[i]);
    for (std::uint32_t i = 0; i < built; ++i)
        m_props[i] = fresh[i];
    m_prop_count = built;
    m_scene = scene;
    m_scene_applied = true;
    ++m_scene_revision;
    return {};
}

hue::Result<void> Encounter::reset(hue::LinearArena& arena, hue::MeshDraw* draws) {
    HUE_PROFILE_ZONE("game::Encounter::reset");
    if (m_scene_pending || !m_scene_applied) {
        const auto applied = apply_scene(m_scene_pending ? m_next_scene : m_scene);
        if (!applied)
            return applied.error();
        m_scene_pending = false;
    }
    m_state = EncounterState::kReady;
    if (m_tuning_pending) {
        m_tuning = m_next_tuning;
        m_tuning_pending = false;
    }
    m_stats = {};
    m_pending = hue::combat::Action::kNone;
    m_toggle_pending = m_cycle_pending = false;
    m_lock_target = 0;
    m_hitstop = 0.0f;
    m_dodge_direction = {};
    m_event_count = 0;
    m_camera = FollowCamera{};
    for (std::uint32_t i = 0; i < kMaxActors; ++i) {
        m_perception[i] = {};
        // Enemy slot 1 keeps the historical seed; later slots diverge from it.
        m_brains[i] = hue::combat::Brain{m_tuning.ai, 2026u + (i > 0 ? i - 1 : 0)};
        m_enemy_consumed[i] = m_player_consumed[i] = 0;
        m_last_state[i] = hue::combat::State::kLocomotion;
    }
    for (std::uint32_t i = 0; i < m_roster.count(); ++i) {
        m_yaw[i] = m_scene.actors[i].yaw;
        auto body = m_physics.reset_character(m_roster.body(i), m_scene.actors[i].position);
        if (!body)
            return body.error();
        arena.reset();
        auto actor = m_roster.actor(i).reset(arena, draws, m_tuning);
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
    if (m_state == EncounterState::kReady) {
        m_state = EncounterState::kFighting;
        push_event(EncounterEventType::kFightStart, kPlayerSlot);
    }
}

bool Encounter::stage_tuning(const GameTuning& tuning) noexcept {
    if (!m_roster.can_apply_tuning(tuning))
        return false;
    m_next_tuning = tuning;
    m_tuning_pending = true;
    return true;
}
void Encounter::buffer(hue::combat::Action action) noexcept {
    if (m_state == EncounterState::kFighting && action != hue::combat::Action::kNone)
        m_pending = action;
}
void Encounter::toggle_lock() noexcept {
    if (m_state == EncounterState::kFighting)
        m_toggle_pending = !m_toggle_pending;
}
void Encounter::cycle_lock() noexcept {
    if (m_state == EncounterState::kFighting)
        m_cycle_pending = true;
}
hue::Vec3 Encounter::position(std::uint32_t actor) const noexcept {
    return m_physics.character_position(m_roster.body(actor));
}
hue::Mat4 Encounter::model(std::uint32_t actor) const noexcept {
    return hue::Mat4::trs(position(actor),
                          hue::Quat::from_axis_angle({0.0f, 1.0f, 0.0f}, m_yaw[actor] + hue::kPi),
                          {1.0f, 1.0f, 1.0f});
}

std::uint32_t Encounter::enemies_alive() const noexcept {
    std::uint32_t alive = 0;
    for (std::uint32_t e = 1; e < m_roster.count(); ++e)
        alive += m_roster.actor(e).alive() ? 1u : 0u;
    return alive;
}

std::uint32_t Encounter::nearest_enemy(float max_range) const noexcept {
    std::uint32_t best = 0;
    float best_distance = max_range;
    for (std::uint32_t e = 1; e < m_roster.count(); ++e) {
        if (!m_roster.actor(e).alive())
            continue;
        const float distance = length(position(e) - position(kPlayerSlot));
        if (distance <= best_distance) {
            // Ties keep the lower slot so the choice is deterministic.
            if (best == 0 || distance < best_distance)
                best = e;
            best_distance = distance;
        }
    }
    return best;
}

std::uint32_t Encounter::focus_enemy() const noexcept {
    if (m_lock_target != 0 && m_lock_target < m_roster.count())
        return m_lock_target;
    const std::uint32_t nearest = nearest_enemy(1.0e9f);
    return nearest != 0 ? nearest : 1;
}

void Encounter::set_lock(std::uint32_t target) noexcept {
    if (target == m_lock_target)
        return;
    m_lock_target = target;
    push_event(target != 0 ? EncounterEventType::kLockOn : EncounterEventType::kLockOff,
               target != 0 ? target : kPlayerSlot);
}

void Encounter::push_event(EncounterEventType type, std::uint32_t actor) noexcept {
    if (m_event_count >= kMaxEncounterEvents)
        return;
    m_events[m_event_count++] = {type, actor,
                                 actor < m_roster.count() ? position(actor) : hue::Vec3{}};
}

std::uint32_t Encounter::drain_events(EncounterEvent* out, std::uint32_t capacity) noexcept {
    const std::uint32_t count = out == nullptr ? 0 : std::min(capacity, m_event_count);
    for (std::uint32_t i = 0; i < count; ++i)
        out[i] = m_events[i];
    m_event_count = 0;
    return count;
}

hue::Result<void> Encounter::tick(hue::Vec3 movement, hue::LinearArena& arena,
                                  hue::MeshDraw* draws) {
    HUE_PROFILE_ZONE("game::Encounter::tick");
    using hue::combat::State;
    const std::uint32_t count = m_roster.count();
    if (count < 2)
        return hue::ErrorCode::kInvalidArgument; // reset() has not applied a scene yet
    CombatCharacter& player = m_roster.actor(kPlayerSlot);
    if (m_state != EncounterState::kFighting) {
        for (std::uint32_t i = 0; i < count; ++i) {
            arena.reset();
            auto pose = m_roster.actor(i).present(kTick, arena, draws);
            if (!pose)
                return pose.error();
        }
        return {};
    }
    ++m_stats.ticks;
    if (m_toggle_pending) {
        set_lock(m_lock_target != 0 ? 0 : nearest_enemy(m_tuning.lock_acquire));
        m_toggle_pending = false;
    }
    if (m_cycle_pending) {
        // Next living enemy in range after the current target, wrapping.
        std::uint32_t next = 0;
        for (std::uint32_t step = 1; step < count && next == 0; ++step) {
            const std::uint32_t e = 1 + (m_lock_target + step - 1) % (count - 1);
            if (m_roster.actor(e).alive() &&
                length(position(e) - position(kPlayerSlot)) <= m_tuning.lock_acquire)
                next = e;
        }
        if (next != 0)
            set_lock(next);
        m_cycle_pending = false;
    }
    if (m_lock_target != 0) {
        if (!m_roster.actor(m_lock_target).alive())
            set_lock(nearest_enemy(m_tuning.lock_acquire)); // move on, or release
        else if (length(position(m_lock_target) - position(kPlayerSlot)) > m_tuning.lock_release)
            set_lock(0);
    }
    if (m_hitstop > 0.0f) {
        m_hitstop = std::max(0.0f, m_hitstop - kTick);
        ++m_stats.hitstop_ticks;
        return {}; // preserve pending action and owned last poses
    }
    player.buffer_action(m_pending);
    m_pending = hue::combat::Action::kNone;
    hue::combat::AiCommand commands[kMaxActors]{};
    for (std::uint32_t e = 1; e < count; ++e) {
        CombatCharacter& enemy = m_roster.actor(e);
        const hue::Vec3 to_player = position(kPlayerSlot) - position(e);
        hue::combat::Perception perception;
        perception.target_alive = player.alive();
        perception.self_alive = enemy.alive();
        perception.line_of_sight = visible(m_physics, position(e), position(kPlayerSlot));
        perception.distance = length(to_player);
        perception.to_target = to_player;
        perception.self_state = enemy.state();
        m_perception[e] = perception;
        commands[e] = m_brains[e].update(kTick, perception);
        enemy.buffer_action(commands[e].action);
    }
    // Controllers do not collide with each other, so crowding enemies steer
    // apart instead of stacking on one spot. A single enemy is unaffected.
    for (std::uint32_t a = 1; a < count; ++a) {
        for (std::uint32_t b = a + 1; b < count; ++b) {
            if (!m_roster.actor(a).alive() || !m_roster.actor(b).alive())
                continue;
            hue::Vec3 apart = position(a) - position(b);
            apart.y = 0.0f;
            const float distance = length(apart);
            if (distance >= kEnemySpacing)
                continue;
            // Coincident enemies split along a fixed axis.
            const hue::Vec3 direction =
                distance > 1.0e-3f ? apart * (1.0f / distance) : hue::Vec3{1.0f, 0.0f, 0.0f};
            const hue::Vec3 push =
                direction * (kEnemySeparationSpeed * (1.0f - distance / kEnemySpacing));
            commands[a].planar_velocity = commands[a].planar_velocity + push;
            commands[b].planar_velocity = commands[b].planar_velocity - push;
        }
    }

    if (!std::isfinite(movement.x) || !std::isfinite(movement.z))
        movement = {};
    movement.y = 0.0f;
    hue::Vec3 desired = movement;
    if (player.state() == State::kDodge) {
        const float decay =
            hue::clamp(1.0f - player.state_seconds() / m_tuning.dodge_seconds, 0.0f, 1.0f);
        desired = m_dodge_direction * (m_tuning.dodge_speed * decay);
    } else if (player.state() != State::kLocomotion) {
        desired = {};
    }
    m_physics.set_character_velocity(m_roster.body(kPlayerSlot), desired);
    for (std::uint32_t e = 1; e < count; ++e)
        m_physics.set_character_velocity(
            m_roster.body(e), m_roster.actor(e).alive() ? commands[e].planar_velocity : hue::Vec3{});
    auto step = m_physics.update(kTick);
    if (!step)
        return step.error();
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto velocity = m_physics.character_velocity(m_roster.body(i));
        const hue::Vec3 planar{velocity.x, 0.0f, velocity.z};
        hue::Vec3 face = planar;
        if (i == kPlayerSlot && m_lock_target != 0)
            face = position(m_lock_target) - position(kPlayerSlot);
        else if (i != kPlayerSlot && commands[i].face_target)
            face = position(kPlayerSlot) - position(i);
        if (length_squared(face) > 0.04f) {
            const float target = std::atan2(-face.x, -face.z);
            m_yaw[i] += hue::damp(0.0f, wrapped(target - m_yaw[i]), 14.0f, kTick);
        }
        m_roster.actor(i).set_locomotion_speed(length(planar));
        arena.reset();
        auto pose = m_roster.actor(i).update(kTick, arena, draws);
        if (!pose)
            return pose.error();
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const State now = m_roster.actor(i).state();
        if (now != m_last_state[i]) {
            if (now == State::kAttackLight)
                push_event(EncounterEventType::kSwingLight, i);
            else if (now == State::kAttackHeavy)
                push_event(EncounterEventType::kSwingHeavy, i);
            else if (now == State::kDodge)
                push_event(EncounterEventType::kDodge, i);
            else if (now == State::kDeath)
                push_event(EncounterEventType::kDeath, i);
            if (i == kPlayerSlot && now == State::kDodge) {
                ++m_stats.dodges;
                m_dodge_direction = length_squared(movement) > 0.01f
                                        ? normalize(movement)
                                        : hue::Vec3{std::sin(m_yaw[0]), 0.0f, std::cos(m_yaw[0])};
            }
        }
        m_last_state[i] = now;
    }

    // Snapshot EVERY contact before applying any. Simultaneous lethal hits
    // trade, with player death resolving to defeat, independent of iteration order.
    bool player_hits[kMaxActors]{};
    bool enemy_hits[kMaxActors]{};
    for (std::uint32_t e = 1; e < count; ++e) {
        const CombatCharacter& enemy = m_roster.actor(e);
        if (!player.alive() || !enemy.alive())
            continue;
        player_hits[e] = !enemy.invulnerable() && player.strike_window() &&
                         player.activation() != m_player_consumed[e] &&
                         player.hitbox_overlaps(model(kPlayerSlot), enemy, model(e), m_physics);
        enemy_hits[e] = !player.invulnerable() && enemy.strike_window() &&
                        enemy.activation() != m_enemy_consumed[e] &&
                        enemy.hitbox_overlaps(model(e), player, model(kPlayerSlot), m_physics);
    }
    const auto resolve = [this](std::uint32_t attacker, std::uint32_t defender,
                                std::uint64_t& consumed) {
        CombatCharacter& source = m_roster.actor(attacker);
        const bool heavy = source.state() == State::kAttackHeavy;
        const auto contact =
            m_roster.actor(defender).apply_strike(heavy ? m_tuning.heavy : m_tuning.light);
        if (!contact.landed)
            return;
        consumed = source.activation();
        const unsigned side = attacker == kPlayerSlot ? 0u : 1u;
        ++m_stats.hits[side];
        if (contact.staggered)
            ++m_stats.staggers[side];
        m_hitstop = std::max(m_hitstop, contact.hitstop_seconds);
        hue::Vec3 direction = position(defender) - position(attacker);
        direction.y = 0.25f;
        m_camera.add_impulse(normalize(direction) * (contact.shake * 6.0f));
        push_event(heavy ? EncounterEventType::kHitHeavy : EncounterEventType::kHitLight, defender);
        if (contact.staggered && !contact.fatal)
            push_event(EncounterEventType::kStagger, defender);
    };
    for (std::uint32_t e = 1; e < count; ++e)
        if (player_hits[e])
            resolve(kPlayerSlot, e, m_player_consumed[e]);
    for (std::uint32_t e = 1; e < count; ++e)
        if (enemy_hits[e])
            resolve(e, kPlayerSlot, m_enemy_consumed[e]);

    if (!player.alive() || enemies_alive() == 0) {
        m_state = !player.alive() ? EncounterState::kLost : EncounterState::kWon;
        set_lock(0);
        m_pending = hue::combat::Action::kNone;
        m_toggle_pending = m_cycle_pending = false;
        m_hitstop = 0.0f;
        for (std::uint32_t i = 0; i < count; ++i)
            m_physics.set_character_velocity(m_roster.body(i), {});
        push_event(m_state == EncounterState::kWon ? EncounterEventType::kVictory
                                                   : EncounterEventType::kDefeat,
                   kPlayerSlot);
        HUE_LOG_INFO("fight_summary outcome=%s seed=2026 ticks=%llu hitstop_ticks=%llu "
                     "hits_landed=%u hits_taken=%u staggers=%u dodges=%u enemies=%u",
                     encounter_state_name(m_state), static_cast<unsigned long long>(m_stats.ticks),
                     static_cast<unsigned long long>(m_stats.hitstop_ticks), m_stats.hits[0],
                     m_stats.hits[1], m_stats.staggers[0], m_stats.dodges, count - 1);
    }
    return {};
}
