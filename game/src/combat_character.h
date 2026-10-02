// game/src/combat_character.h
//
// Week 9: one playable/AI character = skinned mesh + Animator + combat
// state machine. The machine decides states from buffered input, per-tick
// flags, and the anim events the runtime fires; this class maps states to
// clips (locomotion runs the Week 8 idle/walk/run speed blend) and keeps
// the draw slot's joint palette fed.

#pragma once

#include "hue/anim/animation.h"
#include "hue/asset/gltf_loader.h"
#include "hue/asset/mesh_data.h"
#include "hue/combat/combat.h"
#include "hue/core/memory.h"
#include "hue/core/result.h"
#include "hue/physics/physics.h"
#include "hue/render/renderer.h"

#include <cstdint>
#include <utility>

class CombatCharacter {
public:
    CombatCharacter(hue::asset::SkinnedMeshData&& data, hue::anim::AnimationEventTrack&& events,
                    hue::MeshHandle mesh, std::uint32_t draw_index)
        : m_data(std::move(data)), m_events(std::move(events)), m_mesh(mesh),
          m_draw_index(draw_index) {}

    CombatCharacter(const CombatCharacter&) = delete;
    CombatCharacter& operator=(const CombatCharacter&) = delete;

    [[nodiscard]] hue::Result<void> initialize();
    [[nodiscard]] hue::Result<void> reset(hue::LinearArena& arena, hue::MeshDraw* draws);
    // Terminal presentation advances clips without consuming buffered actions.
    [[nodiscard]] hue::Result<void> present(float dt, hue::LinearArena& arena,
                                            hue::MeshDraw* draws);

    // Records a pressed action; the machine buffers and consumes it at the
    // next legal moment (cancel window, state end, or immediately).
    void buffer_action(hue::combat::Action action) noexcept { m_machine.buffer(action); }

    // Ground speed feeding the locomotion idle/walk/run blend.
    void set_locomotion_speed(float speed) noexcept { m_speed = speed; }

    // Incoming damage. Invulnerable frames (dodge i-frames) ignore the
    // hit entirely. apply_damage always staggers; apply_strike uses poise
    // so chip hits can land without a hit-react.
    void apply_damage(float amount) noexcept;
    [[nodiscard]] hue::combat::StrikeContact
    apply_strike(const hue::combat::Strike& strike) noexcept;
    void notify_hit(bool fatal) noexcept {
        m_pending_hit = true;
        m_pending_fatal = m_pending_fatal || fatal;
    }

    [[nodiscard]] hue::Result<void> update(float delta_seconds, hue::LinearArena& arena,
                                           hue::MeshDraw* draws);

    [[nodiscard]] bool hitbox_overlaps(const hue::Mat4& self_model, const CombatCharacter& other,
                                       const hue::Mat4& other_model,
                                       hue::physics::PhysicsWorld& physics) const noexcept;

    [[nodiscard]] hue::combat::State state() const noexcept { return m_machine.state(); }
    [[nodiscard]] float state_seconds() const noexcept { return m_machine.state_seconds(); }
    [[nodiscard]] bool alive() const noexcept { return m_machine.alive() && m_health > 0.0f; }
    [[nodiscard]] bool invulnerable() const noexcept { return m_machine.invulnerable(); }
    [[nodiscard]] bool hitbox_active() const noexcept { return m_machine.hitbox_active(); }
    [[nodiscard]] float health() const noexcept { return m_health; }
    [[nodiscard]] float max_health() const noexcept { return m_max_health; }
    [[nodiscard]] float health_ratio() const noexcept {
        return m_max_health > 0.0f ? m_health / m_max_health : 0.0f;
    }
    [[nodiscard]] float poise() const noexcept { return m_poise; }
    [[nodiscard]] bool cancel_open() const noexcept { return m_machine.cancel_open(); }
    [[nodiscard]] hue::combat::Action buffered_action() const noexcept {
        return m_machine.buffered_action();
    }
    [[nodiscard]] bool strike_window() const noexcept { return m_strike_window; }
    [[nodiscard]] float buffer_remaining() const noexcept { return m_machine.buffer_remaining(); }
    [[nodiscard]] std::uint64_t activation() const noexcept { return m_activation; }
    [[nodiscard]] const hue::combat::BoundVolumeSet& hitboxes() const noexcept {
        return m_hitboxes;
    }
    [[nodiscard]] const hue::combat::BoundVolumeSet& hurtboxes() const noexcept {
        return m_hurtboxes;
    }
    [[nodiscard]] hue::Mat4 volume_transform(const hue::Mat4& model,
                                             const hue::combat::BoundVolume& volume) const noexcept;

private:
    [[nodiscard]] hue::Result<void> start_state_clip(hue::combat::State state);
    void retain_pose(const hue::anim::AnimationFrame& frame, hue::MeshDraw* draws) noexcept;

    hue::asset::SkinnedMeshData m_data;
    hue::anim::AnimationEventTrack m_events;
    hue::anim::Animator m_animator;
    hue::combat::StateMachine m_machine;
    hue::MeshHandle m_mesh;
    std::uint32_t m_draw_index = 0;

    std::uint32_t m_run_clip = 0;
    std::uint32_t m_walk_clip = 0;
    std::uint32_t m_idle_clip = 0;
    std::uint32_t m_attack_clip = 0;
    std::uint32_t m_attack_heavy_clip = 0;
    std::uint32_t m_dodge_clip = 0;
    std::uint32_t m_hit_react_clip = 0;
    std::uint32_t m_death_clip = 0;

    float m_speed = 0.0f;
    float m_health = 100.0f;
    float m_max_health = 100.0f;
    float m_poise = 40.0f;
    float m_max_poise = 40.0f;
    float m_poise_regen_delay = 0.0f;
    bool m_pending_hit = false;
    bool m_pending_fatal = false;

    hue::combat::BoundVolumeSet m_hitboxes{};
    hue::combat::BoundVolumeSet m_hurtboxes{};
    // Never retain scratch pointers across ticks/frames (including hitstop).
    hue::Mat4 m_globals[hue::asset::kGltfMaxJoints]{};
    hue::Mat4 m_skinning[hue::asset::kGltfMaxJoints]{};
    std::uint32_t m_joint_count = 0;
    std::uint64_t m_activation = 0;
    bool m_strike_window = false;
};
