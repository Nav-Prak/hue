// engine/audio/include/hue/audio/audio.h
//
// Audio: bounded WAV parsing, a fixed-voice software mixer, and an output
// device. Sound files are untrusted input, so they go through Hue's own
// parser (PCM16 / float32, mono or stereo, capped size, rate and length);
// miniaudio is compiled with every decoder disabled and only opens the
// playback device. The mixer is pure and device-independent: the game thread
// queues commands through a lock-free single-producer ring and the audio
// thread (or a test) calls mix().

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "hue/core/array.h"
#include "hue/core/memory.h"
#include "hue/core/result.h"

namespace hue::audio {

inline constexpr std::size_t kMaxWavBytes = 4u * 1024u * 1024u;
inline constexpr std::uint32_t kMinSampleRate = 8000;
inline constexpr std::uint32_t kMaxSampleRate = 48000;
inline constexpr std::uint32_t kMaxSoundFrames = kMaxSampleRate * 20u; // 20 s at 48 kHz
inline constexpr std::uint32_t kMaxWavChunks = 64;
inline constexpr std::uint32_t kMaxSounds = 32;
inline constexpr std::uint32_t kMaxVoices = 16;
inline constexpr std::uint32_t kCommandCapacity = 64;
inline constexpr std::uint32_t kOutputRate = 48000; // stereo float output
inline constexpr std::uint32_t kInvalidSound = UINT32_MAX;

// Decoded sound: mono float samples in [-1, 1] at the file's own rate.
struct SoundData {
    Array<float> samples{MemoryTag::kAssets};
    std::uint32_t sample_rate = 0;
};

// Parses a RIFF/WAVE buffer. Accepts integer PCM 16-bit and IEEE float
// 32-bit, one or two channels (stereo is averaged to mono), 8-48 kHz, at
// most kMaxSoundFrames frames. Every chunk offset and size is checked
// against the buffer; a declared size that does not fit is an error, never
// a truncated read. Non-finite float samples reject the file.
[[nodiscard]] Result<SoundData> parse_wav(const void* data, std::size_t size) noexcept;

struct PlayParams {
    float gain = 1.0f;  // clamped to [0, 4]
    float pan = 0.0f;   // -1 left .. +1 right, equal-power
    float pitch = 1.0f; // clamped to [0.25, 4]
    bool loop = false;
};

class Mixer {
public:
    Mixer() = default;
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    // Registers a sound and returns its id. Only before seal(): the sample
    // tables are immutable once the audio thread may be reading them.
    [[nodiscard]] Result<std::uint32_t> add_sound(const SoundData& sound) noexcept;
    void seal() noexcept { m_sealed.store(true, std::memory_order_release); }

    // Game thread. Returns false (and counts a drop) when the id is unknown,
    // a parameter is not finite, or the command ring is full.
    bool play(std::uint32_t sound, const PlayParams& params = {}) noexcept;
    bool stop_all() noexcept;
    void set_master_gain(float gain) noexcept; // clamped to [0, 2]; non-finite -> 0

    // Audio thread (or tests): writes `frames` interleaved stereo frames.
    // Output is always finite and within [-1, 1].
    void mix(float* output, std::uint32_t frames) noexcept;

    [[nodiscard]] std::uint32_t sound_count() const noexcept { return m_sound_count; }
    [[nodiscard]] std::uint32_t active_voices() const noexcept {
        return m_active_voices.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t dropped_commands() const noexcept {
        return m_dropped.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t frames_mixed() const noexcept {
        return m_frames_mixed.load(std::memory_order_relaxed);
    }

private:
    struct Sound {
        float* samples = nullptr; // tagged heap, owned
        std::uint32_t frames = 0;
        std::uint32_t sample_rate = 0;
    };
    struct Command {
        std::uint32_t sound = kInvalidSound; // kInvalidSound = stop all
        float gain_left = 0.0f;
        float gain_right = 0.0f;
        float pitch = 1.0f;
        bool loop = false;
    };
    struct Voice {
        std::uint32_t sound = kInvalidSound;
        double position = 0.0; // in source frames
        double step = 1.0;
        float gain_left = 0.0f;
        float gain_right = 0.0f;
        bool loop = false;
        bool active = false;
    };
    bool push(const Command& command) noexcept;
    void start_voice(const Command& command) noexcept;

    Sound m_sounds[kMaxSounds]{};
    std::uint32_t m_sound_count = 0;
    std::atomic<bool> m_sealed{false};

    Command m_commands[kCommandCapacity]{};
    std::atomic<std::uint32_t> m_write{0}; // game thread
    std::atomic<std::uint32_t> m_read{0};  // audio thread

    Voice m_voices[kMaxVoices]{}; // audio thread only
    std::uint32_t m_next_steal = 0;
    std::atomic<float> m_master_gain{1.0f};
    std::atomic<std::uint32_t> m_active_voices{0};
    std::atomic<std::uint32_t> m_dropped{0};
    std::atomic<std::uint64_t> m_frames_mixed{0};
};

// Playback device driving a sealed Mixer from the platform's audio thread.
// The mixer must outlive the device. Creation fails with kUnsupported when
// the machine has no usable output; callers keep running silently.
class Device {
public:
    [[nodiscard]] static Result<Device> create(Mixer& mixer) noexcept;
    Device(Device&& other) noexcept;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device& operator=(Device&&) = delete;
    ~Device();

private:
    Device() = default;
    void* m_device = nullptr; // ma_device, tagged heap
};

} // namespace hue::audio
