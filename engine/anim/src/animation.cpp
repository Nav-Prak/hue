// engine/anim/src/animation.cpp

#include "hue/anim/animation.h"

#include "hue/asset/gltf_loader.h"
#include "hue/core/trace.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hue::anim {

namespace {

[[nodiscard]] float clamp01(float value) noexcept {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

[[nodiscard]] float clip_time(float time, float duration, bool loop) noexcept {
    if (duration <= 0.0f) return 0.0f;
    if (!loop) {
        if (time < 0.0f) return 0.0f;
        return time > duration ? duration : time;
    }
    float wrapped = std::fmod(time, duration);
    if (wrapped < 0.0f) wrapped += duration;
    return wrapped;
}

void sample_channel(const asset::AnimationChannel& channel, float time,
                    LocalTransform& transform) noexcept {
    const std::size_t count = channel.times.size();
    if (count == 0) return;

    std::size_t upper = 0;
    while (upper < count && channel.times[upper] <= time) ++upper;
    std::size_t a = 0;
    std::size_t b = 0;
    float weight = 0.0f;
    if (upper == 0) {
        a = b = 0;
    } else if (upper >= count) {
        a = b = count - 1;
    } else {
        a = upper - 1;
        b = upper;
        const float span = channel.times[b] - channel.times[a];
        weight = span > 0.0f ? clamp01((time - channel.times[a]) / span) : 0.0f;
    }

    if (channel.path == asset::AnimationPath::kRotation) {
        const std::size_t oa = a * 4;
        const std::size_t ob = b * 4;
        const Quat qa{channel.values[oa], channel.values[oa + 1], channel.values[oa + 2],
                      channel.values[oa + 3]};
        const Quat qb{channel.values[ob], channel.values[ob + 1], channel.values[ob + 2],
                      channel.values[ob + 3]};
        transform.rotation = slerp(qa, qb, weight);
        return;
    }

    const std::size_t oa = a * 3;
    const std::size_t ob = b * 3;
    const Vec3 va{channel.values[oa], channel.values[oa + 1], channel.values[oa + 2]};
    const Vec3 vb{channel.values[ob], channel.values[ob + 1], channel.values[ob + 2]};
    const Vec3 value = lerp(va, vb, weight);
    if (channel.path == asset::AnimationPath::kTranslation) {
        transform.translation = value;
    } else {
        transform.scale = value;
    }
}

class JsonReader {
public:
    JsonReader(const char* data, std::size_t size) : m_data(data), m_size(size) {}

    void whitespace() noexcept {
        while (m_offset < m_size) {
            const char c = m_data[m_offset];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++m_offset;
        }
    }

    [[nodiscard]] bool take(char expected) noexcept {
        whitespace();
        if (m_offset >= m_size || m_data[m_offset] != expected) return false;
        ++m_offset;
        return true;
    }

    [[nodiscard]] bool string(char* out, std::size_t capacity) noexcept {
        whitespace();
        if (capacity == 0 || m_offset >= m_size || m_data[m_offset++] != '"') return false;
        std::size_t written = 0;
        while (m_offset < m_size) {
            char c = m_data[m_offset++];
            if (c == '"') {
                out[written] = '\0';
                return written > 0;
            }
            if (c == '\\') {
                if (m_offset >= m_size) return false;
                const char escaped = m_data[m_offset++];
                if (escaped == '"' || escaped == '\\' || escaped == '/') c = escaped;
                else if (escaped == 'b') c = '\b';
                else if (escaped == 'f') c = '\f';
                else if (escaped == 'n') c = '\n';
                else if (escaped == 'r') c = '\r';
                else if (escaped == 't') c = '\t';
                else return false; // unicode escapes are unnecessary for engine identifiers
            }
            if (static_cast<unsigned char>(c) < 0x20 || written + 1 >= capacity) return false;
            out[written++] = c;
        }
        return false;
    }

    [[nodiscard]] bool number(float& out) noexcept {
        whitespace();
        const std::size_t begin = m_offset;
        while (m_offset < m_size) {
            const char c = m_data[m_offset];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' ||
                c == 'E') {
                ++m_offset;
            } else {
                break;
            }
        }
        const std::size_t length = m_offset - begin;
        if (length == 0 || length >= 64) return false;
        char token[64]{};
        std::memcpy(token, m_data + begin, length);
        char* end = nullptr;
        out = std::strtof(token, &end);
        return end == token + length && std::isfinite(out);
    }

    [[nodiscard]] bool done() noexcept {
        whitespace();
        return m_offset == m_size;
    }

private:
    const char* m_data = nullptr;
    std::size_t m_size = 0;
    std::size_t m_offset = 0;
};

[[nodiscard]] bool parse_event(JsonReader& json, AnimationEvent& event) noexcept {
    if (!json.take('{')) return false;
    bool have_clip = false;
    bool have_name = false;
    bool have_time = false;
    bool first = true;
    while (true) {
        if (json.take('}')) break;
        if (!first && !json.take(',')) return false;
        first = false;
        char key[32]{};
        if (!json.string(key, sizeof(key)) || !json.take(':')) return false;
        if (std::strcmp(key, "clip") == 0) {
            if (have_clip || !json.string(event.clip, sizeof(event.clip))) return false;
            have_clip = true;
        } else if (std::strcmp(key, "name") == 0) {
            if (have_name || !json.string(event.name, sizeof(event.name))) return false;
            have_name = true;
        } else if (std::strcmp(key, "time") == 0) {
            if (have_time || !json.number(event.time) || event.time < 0.0f) return false;
            have_time = true;
        } else {
            return false;
        }
    }
    return have_clip && have_name && have_time;
}

[[nodiscard]] bool event_crossed(float event_time, float previous, float current,
                                 bool wrapped) noexcept {
    if (wrapped) return event_time > previous || event_time <= current;
    return event_time > previous && event_time <= current;
}

} // namespace

Result<Pose> allocate_pose(LinearArena& arena, std::uint32_t joint_count) noexcept {
    if (joint_count == 0 || joint_count > asset::kGltfMaxJoints) {
        return ErrorCode::kInvalidArgument;
    }
    auto local = arena.allocate<LocalTransform>(joint_count);
    if (!local) return local.error();
    auto global = arena.allocate<Mat4>(joint_count);
    if (!global) return global.error();
    auto skinning = arena.allocate<Mat4>(joint_count);
    if (!skinning) return skinning.error();
    return Pose{local.value(), global.value(), skinning.value(), joint_count};
}

Result<void> sample_clip(const asset::Joint* joints, std::uint32_t joint_count,
                         const asset::AnimationClip& clip, float time, bool loop,
                         LocalTransform* out) noexcept {
    HUE_PROFILE_ZONE("anim::sample_clip");
    if (joints == nullptr || out == nullptr || joint_count == 0 ||
        joint_count > asset::kGltfMaxJoints || !std::isfinite(time)) {
        return ErrorCode::kInvalidArgument;
    }
    for (std::uint32_t i = 0; i < joint_count; ++i) {
        out[i] = {joints[i].rest_translation, joints[i].rest_rotation, joints[i].rest_scale};
    }
    const float sampled_time = clip_time(time, clip.duration, loop);
    for (std::size_t c = 0; c < clip.channels.size(); ++c) {
        const asset::AnimationChannel& channel = clip.channels[c];
        if (channel.joint >= joint_count) return ErrorCode::kCorruptData;
        sample_channel(channel, sampled_time, out[channel.joint]);
    }
    return {};
}

Result<void> blend_poses(const LocalTransform* a, const LocalTransform* b,
                         std::uint32_t joint_count, float weight,
                         LocalTransform* out) noexcept {
    HUE_PROFILE_ZONE("anim::blend_poses");
    if (a == nullptr || b == nullptr || out == nullptr || joint_count == 0 ||
        joint_count > asset::kGltfMaxJoints || !std::isfinite(weight)) {
        return ErrorCode::kInvalidArgument;
    }
    const float w = clamp01(weight);
    for (std::uint32_t i = 0; i < joint_count; ++i) {
        out[i].translation = lerp(a[i].translation, b[i].translation, w);
        out[i].rotation = slerp(a[i].rotation, b[i].rotation, w);
        out[i].scale = lerp(a[i].scale, b[i].scale, w);
    }
    return {};
}

Result<void> sample_blend_1d(const asset::Joint* joints, std::uint32_t joint_count,
                             const asset::AnimationClip& lower, float lower_time,
                             float lower_position, const asset::AnimationClip& upper,
                             float upper_time, float upper_position, float parameter, bool loop,
                             LinearArena& arena, LocalTransform* out) noexcept {
    HUE_PROFILE_ZONE("anim::sample_blend_1d");
    if (joints == nullptr || out == nullptr || joint_count == 0 ||
        joint_count > asset::kGltfMaxJoints || !std::isfinite(lower_position) ||
        !std::isfinite(upper_position) ||
        !std::isfinite(parameter) || upper_position <= lower_position) {
        return ErrorCode::kInvalidArgument;
    }
    auto lower_pose = arena.allocate<LocalTransform>(joint_count);
    if (!lower_pose) return lower_pose.error();
    auto upper_pose = arena.allocate<LocalTransform>(joint_count);
    if (!upper_pose) return upper_pose.error();
    auto sampled = sample_clip(joints, joint_count, lower, lower_time, loop, lower_pose.value());
    if (!sampled) return sampled.error();
    sampled = sample_clip(joints, joint_count, upper, upper_time, loop, upper_pose.value());
    if (!sampled) return sampled.error();
    const float weight = (parameter - lower_position) / (upper_position - lower_position);
    return blend_poses(lower_pose.value(), upper_pose.value(), joint_count, weight, out);
}

Result<void> build_skinning_matrices(const asset::Joint* joints, std::uint32_t joint_count,
                                     const LocalTransform* local, Mat4* global,
                                     Mat4* skinning) noexcept {
    HUE_PROFILE_ZONE("anim::build_skinning_matrices");
    if (joints == nullptr || local == nullptr || global == nullptr || skinning == nullptr ||
        joint_count == 0 || joint_count > asset::kGltfMaxJoints) {
        return ErrorCode::kInvalidArgument;
    }
    for (std::uint32_t i = 0; i < joint_count; ++i) {
        const Mat4 local_matrix =
            Mat4::trs(local[i].translation, normalize(local[i].rotation), local[i].scale);
        const std::int32_t parent = joints[i].parent;
        if (parent >= static_cast<std::int32_t>(i) || parent < -1) {
            return ErrorCode::kCorruptData;
        }
        global[i] = parent >= 0 ? global[static_cast<std::uint32_t>(parent)] * local_matrix
                                : local_matrix;
        skinning[i] = global[i] * joints[i].inverse_bind;
    }
    return {};
}

std::int32_t find_clip(const asset::SkinnedMeshData& data, const char* name) noexcept {
    if (name == nullptr) return -1;
    for (std::size_t i = 0; i < data.clips.size(); ++i) {
        if (std::strcmp(data.clips[i].name, name) == 0) return static_cast<std::int32_t>(i);
    }
    return -1;
}

Result<AnimationEventTrack> parse_animation_events(const void* bytes, std::size_t size) noexcept {
    if (bytes == nullptr || size == 0 || size > kMaxEventFileBytes) {
        return ErrorCode::kCorruptData;
    }
    JsonReader json(static_cast<const char*>(bytes), size);
    AnimationEventTrack track;
    if (!json.take('{')) return ErrorCode::kCorruptData;
    char root_key[32]{};
    if (!json.string(root_key, sizeof(root_key)) || std::strcmp(root_key, "events") != 0 ||
        !json.take(':') || !json.take('[')) {
        return ErrorCode::kCorruptData;
    }
    bool first = true;
    while (true) {
        if (json.take(']')) break;
        if (!first && !json.take(',')) return ErrorCode::kCorruptData;
        first = false;
        if (track.events.size() >= kMaxAnimationEvents) return ErrorCode::kCorruptData;
        AnimationEvent event;
        if (!parse_event(json, event)) return ErrorCode::kCorruptData;
        if (!track.events.push_back(event)) return ErrorCode::kOutOfMemory;
    }
    if (!json.take('}') || !json.done()) return ErrorCode::kCorruptData;
    return track;
}

Result<AnimationEventTrack> load_animation_events_file(const char* path) noexcept {
    if (path == nullptr) return ErrorCode::kInvalidArgument;
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) return ErrorCode::kNotFound;
    std::fseek(file, 0, SEEK_END);
    const long file_size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (file_size <= 0 || static_cast<std::size_t>(file_size) > kMaxEventFileBytes) {
        std::fclose(file);
        return ErrorCode::kCorruptData;
    }
    auto memory = heap_allocate(static_cast<std::size_t>(file_size), alignof(char),
                                MemoryTag::kAnimation);
    if (!memory) {
        std::fclose(file);
        return memory.error();
    }
    const std::size_t read =
        std::fread(memory.value(), 1, static_cast<std::size_t>(file_size), file);
    std::fclose(file);
    Result<AnimationEventTrack> parsed =
        read == static_cast<std::size_t>(file_size)
            ? parse_animation_events(memory.value(), static_cast<std::size_t>(file_size))
            : Result<AnimationEventTrack>(ErrorCode::kCorruptData);
    (void)heap_free(memory.value());
    return parsed;
}

Result<void> validate_animation_events(const asset::SkinnedMeshData& data,
                                       const AnimationEventTrack& track) noexcept {
    for (std::size_t i = 0; i < track.events.size(); ++i) {
        const AnimationEvent& event = track.events[i];
        const std::int32_t clip = find_clip(data, event.clip);
        if (clip < 0 || !std::isfinite(event.time) || event.time < 0.0f ||
            event.time > data.clips[static_cast<std::size_t>(clip)].duration) {
            return ErrorCode::kCorruptData;
        }
    }
    return {};
}

Result<void> Animator::bind(const asset::SkinnedMeshData& data,
                            std::uint32_t initial_clip) noexcept {
    if (data.joints.empty() || data.joints.size() > asset::kGltfMaxJoints ||
        initial_clip >= data.clips.size()) {
        return ErrorCode::kInvalidArgument;
    }
    m_data = &data;
    m_current = Source{};
    m_current.lower = initial_clip;
    m_current.upper = initial_clip;
    m_previous = m_current;
    m_fade_elapsed = 0.0f;
    m_fade_duration = 0.0f;
    return {};
}

Result<void> Animator::play(std::uint32_t clip, float fade_seconds, bool loop) noexcept {
    if (m_data == nullptr || clip >= m_data->clips.size() || !std::isfinite(fade_seconds) ||
        fade_seconds < 0.0f) {
        return ErrorCode::kInvalidArgument;
    }
    if (!m_current.blended && clip == m_current.lower && loop == m_current.loop) return {};
    m_previous = m_current;
    m_current = Source{};
    m_current.lower = clip;
    m_current.upper = clip;
    m_current.loop = loop;
    m_fade_elapsed = 0.0f;
    m_fade_duration = fade_seconds;
    return {};
}

Result<void> Animator::play_blend(std::uint32_t lower_clip, std::uint32_t upper_clip,
                                  float lower_position, float upper_position,
                                  float fade_seconds) noexcept {
    if (m_data == nullptr || lower_clip >= m_data->clips.size() ||
        upper_clip >= m_data->clips.size() || lower_clip == upper_clip ||
        !std::isfinite(lower_position) || !std::isfinite(upper_position) ||
        upper_position <= lower_position || !std::isfinite(fade_seconds) ||
        fade_seconds < 0.0f) {
        return ErrorCode::kInvalidArgument;
    }
    // One shared phase drives both gaits, so their durations must agree.
    const float lower_duration = m_data->clips[lower_clip].duration;
    const float upper_duration = m_data->clips[upper_clip].duration;
    if (lower_duration <= 0.0f ||
        std::fabs(lower_duration - upper_duration) > 1.0e-4f) {
        return ErrorCode::kInvalidArgument;
    }
    if (m_current.blended && m_current.lower == lower_clip && m_current.upper == upper_clip) {
        m_current.lower_position = lower_position;
        m_current.upper_position = upper_position;
        return {}; // already in this blend; keep phase and parameter
    }
    m_previous = m_current;
    m_current = Source{};
    m_current.lower = lower_clip;
    m_current.upper = upper_clip;
    m_current.lower_position = lower_position;
    m_current.upper_position = upper_position;
    m_current.parameter = lower_position;
    m_current.blended = true;
    m_fade_elapsed = 0.0f;
    m_fade_duration = fade_seconds;
    return {};
}

void Animator::set_blend_parameter(float parameter) noexcept {
    if (!m_current.blended || !std::isfinite(parameter)) return;
    if (parameter < m_current.lower_position) parameter = m_current.lower_position;
    if (parameter > m_current.upper_position) parameter = m_current.upper_position;
    m_current.parameter = parameter;
}

float Animator::blend_weight(const Source& source) const noexcept {
    if (!source.blended) return 0.0f;
    return clamp01((source.parameter - source.lower_position) /
                   (source.upper_position - source.lower_position));
}

std::uint32_t Animator::dominant_clip(const Source& source) const noexcept {
    return source.blended && blend_weight(source) >= 0.5f ? source.upper : source.lower;
}

std::uint32_t Animator::current_clip() const noexcept { return dominant_clip(m_current); }

Result<void> Animator::sample_source(const Source& source, LinearArena& arena,
                                     LocalTransform* out) noexcept {
    const std::uint32_t joint_count = static_cast<std::uint32_t>(m_data->joints.size());
    if (source.blended) {
        return sample_blend_1d(m_data->joints.data(), joint_count,
                               m_data->clips[source.lower], source.time, source.lower_position,
                               m_data->clips[source.upper], source.time, source.upper_position,
                               source.parameter, source.loop, arena, out);
    }
    return sample_clip(m_data->joints.data(), joint_count, m_data->clips[source.lower],
                       source.time, source.loop, out);
}

Result<AnimationFrame> Animator::update(float delta_seconds, LinearArena& arena,
                                        const AnimationEventTrack* events,
                                        const AnimationEvent** fired_events,
                                        std::uint32_t fired_event_capacity) noexcept {
    HUE_PROFILE_ZONE("anim::Animator::update");
    if (m_data == nullptr || !std::isfinite(delta_seconds) || delta_seconds < 0.0f ||
        (fired_event_capacity > 0 && fired_events == nullptr)) {
        return ErrorCode::kInvalidArgument;
    }
    const asset::AnimationClip& clip = m_data->clips[m_current.lower];
    const float old_time = m_current.time;
    const float advanced = old_time + delta_seconds;
    bool wrapped = false;
    if (m_current.loop && clip.duration > 0.0f && advanced >= clip.duration) wrapped = true;
    m_current.time = clip_time(advanced, clip.duration, m_current.loop);
    if (!m_current.loop && advanced >= clip.duration) m_current.time = clip.duration;

    // Events fire from the dominant side of a blend: footfalls follow the
    // gait the viewer actually sees.
    std::uint32_t fired_count = 0;
    if (events != nullptr) {
        const char* event_clip_name = m_data->clips[dominant_clip(m_current)].name;
        for (std::size_t i = 0; i < events->events.size(); ++i) {
            const AnimationEvent& event = events->events[i];
            if (std::strcmp(event.clip, event_clip_name) == 0 &&
                event_crossed(event.time, old_time, m_current.time, wrapped)) {
                if (fired_count >= fired_event_capacity) return ErrorCode::kOutOfMemory;
                fired_events[fired_count++] = &event;
            }
        }
    }

    const std::uint32_t joint_count = static_cast<std::uint32_t>(m_data->joints.size());
    auto pose = allocate_pose(arena, joint_count);
    if (!pose) return pose.error();
    auto sampled = sample_source(m_current, arena, pose.value().local);
    if (!sampled) return sampled.error();

    if (m_fade_elapsed < m_fade_duration) {
        const float previous_duration = m_data->clips[m_previous.lower].duration;
        m_previous.time =
            clip_time(m_previous.time + delta_seconds, previous_duration, m_previous.loop);
        auto previous_pose = arena.allocate<LocalTransform>(joint_count);
        if (!previous_pose) return previous_pose.error();
        sampled = sample_source(m_previous, arena, previous_pose.value());
        if (!sampled) return sampled.error();
        m_fade_elapsed += delta_seconds;
        const float weight = m_fade_duration > 0.0f ? m_fade_elapsed / m_fade_duration : 1.0f;
        auto blended = blend_poses(previous_pose.value(), pose.value().local, joint_count, weight,
                                   pose.value().local);
        if (!blended) return blended.error();
    }

    auto built = build_skinning_matrices(m_data->joints.data(), joint_count, pose.value().local,
                                         pose.value().global, pose.value().skinning);
    if (!built) return built.error();
    return AnimationFrame{pose.value().skinning, joint_count, fired_count};
}

bool Animator::finished() const noexcept {
    if (m_data == nullptr || m_current.loop) return false;
    return m_current.time >= m_data->clips[m_current.lower].duration;
}

} // namespace hue::anim
