// engine/audio/src/mixer.cpp
//
// Fixed-voice mixer. No allocation, locks or logging on the audio thread:
// commands arrive through a single-producer/single-consumer ring and all
// voice state is touched only inside mix().

#include "hue/audio/audio.h"

#include <cmath>
#include <cstring>

namespace hue::audio {

namespace {
constexpr float kQuarterPi = 0.78539816339f;

[[nodiscard]] float clamped(float value, float low, float high) noexcept {
    return value < low ? low : (value > high ? high : value);
}
} // namespace

Mixer::~Mixer() {
    for (auto& sound : m_sounds) {
        if (sound.samples != nullptr) {
            (void)heap_free(sound.samples);
            sound.samples = nullptr;
        }
    }
}

Result<std::uint32_t> Mixer::add_sound(const SoundData& sound) noexcept {
    if (m_sealed.load(std::memory_order_acquire) || m_sound_count >= kMaxSounds) {
        return ErrorCode::kInvalidArgument;
    }
    const std::size_t frames = sound.samples.size();
    if (frames == 0 || frames > kMaxSoundFrames || sound.sample_rate < kMinSampleRate ||
        sound.sample_rate > kMaxSampleRate) {
        return ErrorCode::kInvalidArgument;
    }
    auto memory = heap_allocate(frames * sizeof(float), alignof(float), MemoryTag::kAssets);
    if (!memory) {
        return memory.error();
    }
    auto* samples = static_cast<float*>(memory.value());
    for (std::size_t i = 0; i < frames; ++i) {
        // Callers other than parse_wav may hand over anything; keep the
        // table finite and bounded so mix() needs no per-sample checks.
        const float value = sound.samples[i];
        samples[i] = std::isfinite(value) ? clamped(value, -1.0f, 1.0f) : 0.0f;
    }
    const std::uint32_t id = m_sound_count;
    m_sounds[id] = {samples, static_cast<std::uint32_t>(frames), sound.sample_rate};
    ++m_sound_count;
    return id;
}

bool Mixer::push(const Command& command) noexcept {
    const std::uint32_t write = m_write.load(std::memory_order_relaxed);
    const std::uint32_t read = m_read.load(std::memory_order_acquire);
    if (write - read >= kCommandCapacity) {
        m_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    m_commands[write % kCommandCapacity] = command;
    m_write.store(write + 1, std::memory_order_release);
    return true;
}

bool Mixer::play(std::uint32_t sound, const PlayParams& params) noexcept {
    if (sound >= m_sound_count || !std::isfinite(params.gain) || !std::isfinite(params.pan) ||
        !std::isfinite(params.pitch)) {
        m_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const float gain = clamped(params.gain, 0.0f, 4.0f);
    const float angle = (clamped(params.pan, -1.0f, 1.0f) + 1.0f) * kQuarterPi;
    Command command;
    command.sound = sound;
    command.gain_left = gain * std::cos(angle);
    command.gain_right = gain * std::sin(angle);
    command.pitch = clamped(params.pitch, 0.25f, 4.0f);
    command.loop = params.loop;
    return push(command);
}

bool Mixer::stop_all() noexcept {
    return push(Command{});
}

void Mixer::set_master_gain(float gain) noexcept {
    m_master_gain.store(std::isfinite(gain) ? clamped(gain, 0.0f, 2.0f) : 0.0f,
                        std::memory_order_relaxed);
}

void Mixer::start_voice(const Command& command) noexcept {
    if (command.sound >= m_sound_count) {
        return;
    }
    Voice* voice = nullptr;
    for (auto& candidate : m_voices) {
        if (!candidate.active) {
            voice = &candidate;
            break;
        }
    }
    if (voice == nullptr) {
        // Every voice is busy: replace one-shots in rotation, never a loop.
        for (std::uint32_t tries = 0; tries < kMaxVoices && voice == nullptr; ++tries) {
            Voice& candidate = m_voices[m_next_steal];
            m_next_steal = (m_next_steal + 1) % kMaxVoices;
            if (!candidate.loop) {
                voice = &candidate;
            }
        }
    }
    if (voice == nullptr) {
        return;
    }
    const Sound& sound = m_sounds[command.sound];
    voice->sound = command.sound;
    voice->position = 0.0;
    voice->step = static_cast<double>(sound.sample_rate) / static_cast<double>(kOutputRate) *
                  static_cast<double>(command.pitch);
    voice->gain_left = command.gain_left;
    voice->gain_right = command.gain_right;
    voice->loop = command.loop;
    voice->active = true;
}

void Mixer::mix(float* output, std::uint32_t frames) noexcept {
    if (output == nullptr || frames == 0) {
        return;
    }
    std::memset(output, 0, static_cast<std::size_t>(frames) * 2u * sizeof(float));

    std::uint32_t read = m_read.load(std::memory_order_relaxed);
    const std::uint32_t write = m_write.load(std::memory_order_acquire);
    for (; read != write; ++read) {
        const Command command = m_commands[read % kCommandCapacity];
        if (command.sound == kInvalidSound) {
            for (auto& voice : m_voices) {
                voice.active = false;
            }
        } else {
            start_voice(command);
        }
    }
    m_read.store(read, std::memory_order_release);

    std::uint32_t active = 0;
    for (auto& voice : m_voices) {
        if (!voice.active) {
            continue;
        }
        const Sound& sound = m_sounds[voice.sound];
        const double length = static_cast<double>(sound.frames);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            if (voice.position >= length) {
                if (!voice.loop) {
                    voice.active = false;
                    break;
                }
                voice.position = std::fmod(voice.position, length);
            }
            const auto index = static_cast<std::uint32_t>(voice.position);
            const std::uint32_t next =
                index + 1 < sound.frames ? index + 1 : (voice.loop ? 0u : index);
            const float blend = static_cast<float>(voice.position - static_cast<double>(index));
            const float sample =
                sound.samples[index] + (sound.samples[next] - sound.samples[index]) * blend;
            const std::size_t at = static_cast<std::size_t>(frame) * 2u;
            output[at] += sample * voice.gain_left;
            output[at + 1u] += sample * voice.gain_right;
            voice.position += voice.step;
        }
        active += voice.active ? 1u : 0u;
    }

    const float master = m_master_gain.load(std::memory_order_relaxed);
    const std::size_t sample_count = static_cast<std::size_t>(frames) * 2u;
    for (std::size_t i = 0; i < sample_count; ++i) {
        const float value = output[i] * master;
        output[i] = std::isfinite(value) ? clamped(value, -1.0f, 1.0f) : 0.0f;
    }
    m_active_voices.store(active, std::memory_order_relaxed);
    m_frames_mixed.fetch_add(frames, std::memory_order_relaxed);
}

} // namespace hue::audio
