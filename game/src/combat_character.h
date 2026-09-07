// game/src/combat_character.h
//
// Week 9: one playable/AI character = skinned mesh + Animator + combat
// state machine. The machine decides states from buffered input, per-tick
// flags, and the anim events the runtime fires; this class maps states to
// clips (locomotion runs the Week 8 idle/walk/run speed blend) and keeps
// the draw slot's joint palette fed.

#pragma once

#include "hue/anim/animation.h"
#include "hue/asset/mesh_data.h"
#include "hue/combat/combat.h"
#include "hue/core/memory.h"
#include "hue/core/result.h"
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

    // Records a pressed action; the machine buffers and consumes it at the
    // next legal moment (cancel window, state end, or immediately).
    void buffer_action(hue::combat::Action action) noexcept { m_machine.buffer(action); }

    // Ground speed feeding the locomotion idle/walk/run blend.
    void set_locomotion_speed(float speed) noexcept { m_speed = speed; }

    // Incoming damage, consumed on the next tick. Invulnerable frames
    // (dodge i-frames) ignore the hit entirely.
    void apply_damage(float amount) noexcept;
    void notify_hit(bool fatal) noexcept {
        m_pending_hit = true;
        m_pending_fatal = m_pending_fatal || fatal;
    }

    [[nodiscard]] hue::Result<void> update(float delta_seconds, hue::LinearArena& arena,
                                           hue::MeshDraw* draws);

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

private:
    [[nodiscard]] hue::Result<void> start_state_clip(hue::combat::State state);

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
    bool m_pending_hit = false;
    bool m_pending_fatal = false;
};
