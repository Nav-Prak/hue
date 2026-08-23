// engine/anim/include/hue/anim/animation.h
//
// Frame-arena skeletal animation runtime. Imported glTF clips are sampled
// into local TRS poses, blended, propagated through the topologically sorted
// skeleton, and converted to GPU skinning matrices. Sidecar event JSON is a
// bounded untrusted-input boundary just like the glTF importer.

#pragma once

#include <cstddef>
#include <cstdint>

#include "hue/asset/mesh_data.h"
#include "hue/core/array.h"
#include "hue/core/memory.h"
#include "hue/core/result.h"

namespace hue::anim {

inline constexpr std::size_t kMaxEventFileBytes = 64u * 1024u;
inline constexpr std::uint32_t kMaxAnimationEvents = 256;
inline constexpr std::uint32_t kEventNameCapacity = 64;

struct LocalTransform {
    Vec3 translation{};
    Quat rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct Pose {
    LocalTransform* local = nullptr;
    Mat4* global = nullptr;
    Mat4* skinning = nullptr;
    std::uint32_t joint_count = 0;
};

struct AnimationEvent {
    char clip[asset::kClipNameCapacity] = {};
    char name[kEventNameCapacity] = {};
    float time = 0.0f;
};

struct AnimationEventTrack {
    Array<AnimationEvent> events{MemoryTag::kAnimation};
};

struct AnimationFrame {
    const Mat4* skinning_matrices = nullptr;
    std::uint32_t joint_count = 0;
    std::uint32_t fired_event_count = 0;
};

[[nodiscard]] Result<Pose> allocate_pose(LinearArena& arena, std::uint32_t joint_count) noexcept;

[[nodiscard]] Result<void> sample_clip(const asset::Joint* joints, std::uint32_t joint_count,
                                       const asset::AnimationClip& clip, float time, bool loop,
                                       LocalTransform* out) noexcept;

[[nodiscard]] Result<void> blend_poses(const LocalTransform* a, const LocalTransform* b,
                                       std::uint32_t joint_count, float weight,
                                       LocalTransform* out) noexcept;

// Generic two-sample 1D blend. Positions may be any ordered speed values;
// the parameter is clamped to their interval. Scratch poses use the frame arena.
[[nodiscard]] Result<void>
sample_blend_1d(const asset::Joint* joints, std::uint32_t joint_count,
                const asset::AnimationClip& lower, float lower_time, float lower_position,
                const asset::AnimationClip& upper, float upper_time, float upper_position,
                float parameter, bool loop, LinearArena& arena, LocalTransform* out) noexcept;

[[nodiscard]] Result<void> build_skinning_matrices(const asset::Joint* joints,
                                                   std::uint32_t joint_count,
                                                   const LocalTransform* local, Mat4* global,
                                                   Mat4* skinning) noexcept;

[[nodiscard]] std::int32_t find_clip(const asset::SkinnedMeshData& data,
                                     const char* name) noexcept;

[[nodiscard]] Result<AnimationEventTrack> parse_animation_events(const void* bytes,
                                                                 std::size_t size) noexcept;
[[nodiscard]] Result<AnimationEventTrack> load_animation_events_file(const char* path) noexcept;
[[nodiscard]] Result<void> validate_animation_events(const asset::SkinnedMeshData& data,
                                                     const AnimationEventTrack& track) noexcept;

class Animator {
public:
    Animator() = default;

    [[nodiscard]] Result<void> bind(const asset::SkinnedMeshData& data,
                                    std::uint32_t initial_clip = 0) noexcept;
    [[nodiscard]] Result<void> play(std::uint32_t clip, float fade_seconds = 0.15f,
                                    bool loop = true) noexcept;

    [[nodiscard]] Result<AnimationFrame>
    update(float delta_seconds, LinearArena& arena, const AnimationEventTrack* events = nullptr,
           const AnimationEvent** fired_events = nullptr,
           std::uint32_t fired_event_capacity = 0) noexcept;

    [[nodiscard]] std::uint32_t current_clip() const noexcept { return m_current_clip; }
    [[nodiscard]] float current_time() const noexcept { return m_current_time; }
    [[nodiscard]] bool finished() const noexcept;

private:
    const asset::SkinnedMeshData* m_data = nullptr;
    std::uint32_t m_current_clip = 0;
    std::uint32_t m_previous_clip = 0;
    float m_current_time = 0.0f;
    float m_previous_time = 0.0f;
    float m_fade_elapsed = 0.0f;
    float m_fade_duration = 0.0f;
    bool m_loop = true;
    bool m_previous_loop = true;
};

} // namespace hue::anim
