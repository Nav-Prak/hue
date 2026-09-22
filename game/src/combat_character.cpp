// game/src/combat_character.cpp

#include "combat_character.h"

#include "hue/asset/gltf_loader.h"
#include "hue/core/log.h"
#include "hue/core/trace.h"

#include <cstring>

namespace {

// Movement tuning shared with main.cpp: the blend parameter is literally
// the capsule controller's solved ground speed.
constexpr float kWalkSpeed = 1.6f;
constexpr float kRunSpeed = 4.0f;
constexpr float kIdleThreshold = 0.2f;
constexpr float kPoiseRegenDelay = 1.0f;
constexpr float kPoiseRegenPerSecond = 18.0f;

[[nodiscard]] hue::Mat4 volume_world(const hue::Mat4& model, const hue::Mat4* globals,
                                     std::uint32_t joint_count,
                                     const hue::combat::BoundVolume& volume) noexcept {
    if (globals == nullptr || volume.joint < 0 ||
        static_cast<std::uint32_t>(volume.joint) >= joint_count) {
        return hue::Mat4::identity();
    }
    return model * globals[volume.joint] *
           hue::Mat4::trs(volume.local_offset, {}, {1.0f, 1.0f, 1.0f});
}

[[nodiscard]] bool volumes_overlap(const hue::combat::BoundVolume& hit, const hue::Mat4& hit_world,
                                   const hue::combat::BoundVolume& hurt,
                                   const hue::Mat4& hurt_world,
                                   hue::physics::PhysicsWorld& physics) noexcept {
    using hue::combat::VolumeKind;
    const bool hit_sphere = hit.kind == VolumeKind::kSphere || hit.half_height <= 1.0e-4f;
    const bool hurt_sphere = hurt.kind == VolumeKind::kSphere || hurt.half_height <= 1.0e-4f;
    const hue::Vec3 hit_center{hit_world.m[12], hit_world.m[13], hit_world.m[14]};
    const hue::Vec3 hurt_center{hurt_world.m[12], hurt_world.m[13], hurt_world.m[14]};
    if (hit_sphere && hurt_sphere) {
        return physics.overlap_spheres(hit_center, hit.radius, hurt_center, hurt.radius);
    }
    if (hit_sphere) {
        return physics.overlap_capsule_sphere(hurt_world, hurt.half_height, hurt.radius, hit_center,
                                              hit.radius);
    }
    if (hurt_sphere) {
        return physics.overlap_capsule_sphere(hit_world, hit.half_height, hit.radius, hurt_center,
                                              hurt.radius);
    }
    return physics.overlap_capsules(hit_world, hit.half_height, hit.radius, hurt_world,
                                    hurt.half_height, hurt.radius);
}

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
        {"locomotion", &m_run_clip},
        {"walk", &m_walk_clip},
        {"idle", &m_idle_clip},
        {"attack", &m_attack_clip},
        {"attack_heavy", &m_attack_heavy_clip},
        {"dodge", &m_dodge_clip},
        {"hit_react", &m_hit_react_clip},
        {"death", &m_death_clip},
    };
    for (const ClipBinding& binding : bindings) {
        const std::int32_t clip = hue::anim::find_clip(m_data, binding.name);
        if (clip < 0) {
            HUE_LOG_ERROR("character is missing required clip '%s'", binding.name);
            return hue::ErrorCode::kCorruptData;
        }
        *binding.slot = static_cast<std::uint32_t>(clip);
    }

    const char* joint_names[hue::asset::kGltfMaxJoints] = {};
    const std::uint32_t joint_count = static_cast<std::uint32_t>(m_data.joints.size());
    for (std::uint32_t i = 0; i < joint_count && i < hue::asset::kGltfMaxJoints; ++i) {
        joint_names[i] = m_data.joints[i].name;
    }
    hue::combat::bind_volumes(hue::combat::kDefaultHitboxes, hue::combat::kDefaultHitboxCount,
                              joint_names, joint_count, m_hitboxes);
    hue::combat::bind_volumes(hue::combat::kDefaultHurtboxes, hue::combat::kDefaultHurtboxCount,
                              joint_names, joint_count, m_hurtboxes);
    if (m_hitboxes.count == 0 || m_hurtboxes.count == 0) {
        HUE_LOG_ERROR("character is missing combat joints (hitboxes=%u hurtboxes=%u)",
                      m_hitboxes.count, m_hurtboxes.count);
        return hue::ErrorCode::kCorruptData;
    }

    return m_animator.bind(m_data, m_idle_clip);
}

void CombatCharacter::apply_damage(float amount) noexcept {
    hue::combat::Strike strike;
    strike.health = amount;
    strike.poise = m_max_poise + 1.0f;
    strike.hitstop_seconds = 0.0f;
    strike.shake = 0.0f;
    (void)apply_strike(strike);
}

hue::combat::StrikeContact
CombatCharacter::apply_strike(const hue::combat::Strike& strike) noexcept {
    hue::combat::StrikeContact contact;
    if (strike.health <= 0.0f || m_health <= 0.0f || m_machine.invulnerable() ||
        m_machine.state() == hue::combat::State::kDeath) {
        return contact;
    }
    m_health -= strike.health;
    if (m_health < 0.0f) {
        m_health = 0.0f;
    }
    m_poise -= strike.poise;
    m_poise_regen_delay = kPoiseRegenDelay;
    contact.landed = true;
    contact.fatal = m_health <= 0.0f;
    contact.staggered = contact.fatal || m_poise <= 0.0f;
    contact.hitstop_seconds = strike.hitstop_seconds;
    contact.shake = strike.shake;
    if (m_poise < 0.0f) {
        m_poise = 0.0f;
    }
    if (contact.staggered) {
        notify_hit(contact.fatal);
        if (!contact.fatal) {
            m_poise = m_max_poise; // stagger resets poise so the next string can chip
        }
    }
    return contact;
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
    HUE_PROFILE_ZONE("game::CombatCharacter::update");
    using hue::combat::State;

    if (m_poise_regen_delay > 0.0f) {
        m_poise_regen_delay -= delta_seconds;
    } else if (m_poise < m_max_poise && m_health > 0.0f) {
        m_poise += kPoiseRegenPerSecond * delta_seconds;
        if (m_poise > m_max_poise) {
            m_poise = m_max_poise;
        }
    }

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

    m_strike_window = m_machine.hitbox_active();
    const hue::anim::AnimationEvent* fired[hue::anim::kMaxAnimationEvents]{};
    auto frame =
        m_animator.update(delta_seconds, arena, &m_events, fired, hue::anim::kMaxAnimationEvents);
    if (!frame) {
        return frame.error();
    }
    for (std::uint32_t i = 0; i < frame.value().fired_event_count; ++i) {
        if (std::strcmp(fired[i]->name, "hit_begin") == 0) {
            ++m_activation;
            m_strike_window = true;
        }
        m_machine.handle_event(fired[i]->name);
        HUE_LOG_DEBUG("anim event %s/%.3f: %s (hitbox=%d cancel=%d iframe=%d)", fired[i]->clip,
                      static_cast<double>(fired[i]->time), fired[i]->name,
                      m_machine.hitbox_active() ? 1 : 0, m_machine.cancel_open() ? 1 : 0,
                      m_machine.invulnerable() ? 1 : 0);
    }

    retain_pose(frame.value(), draws);
    return {};
}

void CombatCharacter::retain_pose(const hue::anim::AnimationFrame& frame,
                                  hue::MeshDraw* draws) noexcept {
    m_joint_count = frame.joint_count;
    for (std::uint32_t i = 0; i < m_joint_count; ++i) {
        m_globals[i] = frame.global_matrices[i];
        m_skinning[i] = frame.skinning_matrices[i];
    }
    draws[m_draw_index].mesh = m_mesh;
    draws[m_draw_index].joint_matrices = m_skinning;
    draws[m_draw_index].joint_count = m_joint_count;
}

hue::Result<void> CombatCharacter::reset(hue::LinearArena& arena, hue::MeshDraw* draws) {
    m_machine = hue::combat::StateMachine{};
    m_animator = hue::anim::Animator{};
    m_health = m_max_health;
    m_poise = m_max_poise;
    m_poise_regen_delay = m_speed = 0.0f;
    m_pending_hit = m_pending_fatal = m_strike_window = false;
    m_activation = 0;
    auto bound = m_animator.bind(m_data, m_idle_clip);
    if (!bound)
        return bound.error();
    return present(0.0f, arena, draws);
}

hue::Result<void> CombatCharacter::present(float dt, hue::LinearArena& arena,
                                           hue::MeshDraw* draws) {
    m_machine.stop_actions();
    if (m_pending_fatal) {
        m_machine.update(0.0f, {false, true, true});
        auto played = start_state_clip(hue::combat::State::kDeath);
        if (!played)
            return played.error();
    }
    m_pending_hit = m_pending_fatal = false;
    m_strike_window = false;
    auto frame = m_animator.update(dt, arena);
    if (!frame)
        return frame.error();
    retain_pose(frame.value(), draws);
    return {};
}

hue::Mat4 CombatCharacter::volume_transform(const hue::Mat4& model,
                                            const hue::combat::BoundVolume& volume) const noexcept {
    return volume_world(model, m_globals, m_joint_count, volume);
}

bool CombatCharacter::hitbox_overlaps(const hue::Mat4& self_model, const CombatCharacter& other,
                                      const hue::Mat4& other_model,
                                      hue::physics::PhysicsWorld& physics) const noexcept {
    if (m_joint_count == 0 || other.m_joint_count == 0) {
        return false;
    }
    for (std::uint32_t i = 0; i < m_hitboxes.count; ++i) {
        const hue::combat::BoundVolume& hit = m_hitboxes.items[i];
        const hue::Mat4 hit_world = volume_world(self_model, m_globals, m_joint_count, hit);
        for (std::uint32_t j = 0; j < other.m_hurtboxes.count; ++j) {
            const hue::combat::BoundVolume& hurt = other.m_hurtboxes.items[j];
            const hue::Mat4 hurt_world =
                volume_world(other_model, other.m_globals, other.m_joint_count, hurt);
            if (volumes_overlap(hit, hit_world, hurt, hurt_world, physics)) {
                return true;
            }
        }
    }
    return false;
}
