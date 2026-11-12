// engine/audio/src/wav.cpp
//
// Bounded RIFF/WAVE reader. Nothing here trusts a size field: every chunk is
// checked against the bytes actually present before it is read.

#include "hue/audio/audio.h"

#include "hue/core/checked_math.h"
#include "hue/core/trace.h"

#include <cmath>
#include <cstring>

namespace hue::audio {

namespace {

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatFloat = 3;

[[nodiscard]] std::uint16_t read_u16(const unsigned char* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
[[nodiscard]] std::uint32_t read_u32(const unsigned char* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

} // namespace

Result<SoundData> parse_wav(const void* data, std::size_t size) noexcept {
    HUE_PROFILE_ZONE("audio::parse_wav");
    if (data == nullptr || size < 12 || size > kMaxWavBytes) {
        return ErrorCode::kCorruptData;
    }
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (std::memcmp(bytes, "RIFF", 4) != 0 || std::memcmp(bytes + 8, "WAVE", 4) != 0) {
        return ErrorCode::kCorruptData;
    }
    // The RIFF size may be smaller than the buffer (trailing bytes are
    // ignored) but may never claim more than is there.
    const std::size_t riff_size = read_u32(bytes + 4);
    std::size_t riff_end = 0;
    if (riff_size < 4 || !checked_add(riff_size, 8, riff_end) || riff_end > size) {
        return ErrorCode::kCorruptData;
    }

    bool have_format = false;
    std::uint16_t format = 0;
    std::uint16_t channels = 0;
    std::uint16_t bits = 0;
    std::uint16_t block_align = 0;
    std::uint32_t sample_rate = 0;
    const unsigned char* payload = nullptr;
    std::size_t payload_size = 0;

    std::size_t offset = 12;
    for (std::uint32_t chunk = 0; offset < riff_end; ++chunk) {
        std::size_t body = 0;
        if (chunk >= kMaxWavChunks || !checked_add(offset, 8, body) || body > riff_end) {
            return ErrorCode::kCorruptData;
        }
        const std::size_t chunk_size = read_u32(bytes + offset + 4);
        std::size_t chunk_end = 0;
        if (!checked_add(body, chunk_size, chunk_end) || chunk_end > riff_end) {
            return ErrorCode::kCorruptData;
        }
        if (std::memcmp(bytes + offset, "fmt ", 4) == 0) {
            if (have_format || chunk_size < 16) {
                return ErrorCode::kCorruptData;
            }
            const unsigned char* f = bytes + body;
            format = read_u16(f);
            channels = read_u16(f + 2);
            sample_rate = read_u32(f + 4);
            block_align = read_u16(f + 12);
            bits = read_u16(f + 14);
            have_format = true;
        } else if (std::memcmp(bytes + offset, "data", 4) == 0) {
            if (!have_format || payload != nullptr) {
                return ErrorCode::kCorruptData; // data before fmt, or two data chunks
            }
            payload = bytes + body;
            payload_size = chunk_size;
        }
        // Chunks are word-aligned; the pad byte may be absent only at the very end.
        offset = chunk_end + (chunk_size & 1u);
    }
    if (!have_format || payload == nullptr) {
        return ErrorCode::kCorruptData;
    }
    const bool pcm = format == kFormatPcm && bits == 16;
    const bool ieee = format == kFormatFloat && bits == 32;
    if (!pcm && !ieee) {
        return ErrorCode::kUnsupported;
    }
    if (channels < 1 || channels > 2 || sample_rate < kMinSampleRate ||
        sample_rate > kMaxSampleRate) {
        return ErrorCode::kUnsupported;
    }
    const std::size_t sample_bytes = bits / 8u;
    const std::size_t frame_bytes = sample_bytes * channels;
    if (block_align != frame_bytes || payload_size == 0 || payload_size % frame_bytes != 0) {
        return ErrorCode::kCorruptData;
    }
    const std::size_t frames = payload_size / frame_bytes;
    if (frames > kMaxSoundFrames) {
        return ErrorCode::kCorruptData;
    }

    SoundData sound;
    sound.sample_rate = sample_rate;
    if (!sound.samples.reserve(frames)) {
        return ErrorCode::kOutOfMemory;
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
        float mixed = 0.0f;
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const unsigned char* p = payload + frame * frame_bytes + channel * sample_bytes;
            float value = 0.0f;
            if (pcm) {
                value = static_cast<float>(static_cast<std::int16_t>(read_u16(p))) / 32768.0f;
            } else {
                const std::uint32_t raw = read_u32(p);
                std::memcpy(&value, &raw, sizeof(value));
                if (!std::isfinite(value)) {
                    return ErrorCode::kCorruptData;
                }
            }
            mixed += value;
        }
        mixed /= static_cast<float>(channels);
        if (mixed > 1.0f) {
            mixed = 1.0f;
        } else if (mixed < -1.0f) {
            mixed = -1.0f;
        }
        if (!sound.samples.push_back(mixed)) {
            return ErrorCode::kOutOfMemory;
        }
    }
    return sound;
}

} // namespace hue::audio
