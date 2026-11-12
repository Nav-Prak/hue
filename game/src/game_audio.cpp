// game/src/game_audio.cpp
#include "game_audio.h"

#include "hue/core/log.h"
#include "hue/core/memory.h"
#include "runtime_files.h"

#include <cmath>
#include <cstdio>
#include <utility>

namespace {
// Indexed by EncounterEventType. Fixed names: nothing outside this list can
// be opened through the audio path.
constexpr const char* kSoundFiles[kGameSoundCount] = {
    "fight_start", "swing_light", "swing_heavy", "hit_light", "hit_heavy", "stagger",
    "dodge",       "death",       "lock_on",     "lock_off",  "victory",   "defeat",
};
constexpr float kSoundGain[kGameSoundCount] = {
    0.6f, 0.55f, 0.7f, 0.9f, 1.0f, 0.7f, 0.5f, 0.8f, 0.45f, 0.4f, 0.7f, 0.7f,
};
constexpr bool kSpatial[kGameSoundCount] = {
    false, true, true, true, true, true, true, true, false, false, false, false,
};
static_assert(static_cast<std::uint32_t>(EncounterEventType::kDefeat) + 1 == kGameSoundCount);
} // namespace

std::uint32_t GameAudio::load() {
    for (auto& id : m_ids)
        id = hue::audio::kInvalidSound;
    m_loaded = 0;
    auto buffer = hue::heap_allocate(hue::audio::kMaxWavBytes, 16, hue::MemoryTag::kAssets);
    if (!buffer) {
        HUE_LOG_WARN("audio: no memory for the sound load buffer");
        return 0;
    }
    auto* bytes = static_cast<char*>(buffer.value());
    for (std::uint32_t i = 0; i < kGameSoundCount; ++i) {
        char relative[96];
        std::snprintf(relative, sizeof(relative), "assets/audio/%s.wav", kSoundFiles[i]);
        char path[1024];
        if (!runtime_path(path, sizeof(path), relative))
            continue;
        const auto size = read_bounded_file(path, bytes, hue::audio::kMaxWavBytes);
        if (!size) {
            HUE_LOG_WARN("audio: %s is missing or too large; that effect is silent", relative);
            continue;
        }
        auto sound = hue::audio::parse_wav(bytes, size.value());
        if (!sound) {
            HUE_LOG_WARN("audio: %s was rejected; that effect is silent", relative);
            continue;
        }
        const auto id = m_mixer.add_sound(sound.value());
        if (!id)
            continue;
        m_ids[i] = id.value();
        ++m_loaded;
    }
    (void)hue::heap_free(bytes);
    HUE_LOG_INFO("audio: %u of %u effects loaded", m_loaded, kGameSoundCount);
    return m_loaded;
}

bool GameAudio::open_device() {
    if (m_device)
        return true;
    auto device = hue::audio::Device::create(m_mixer);
    if (!device)
        return false;
    m_device.emplace(std::move(device.value()));
    return true;
}

void GameAudio::set_muted(bool muted) noexcept {
    m_muted = muted;
    m_mixer.set_master_gain(muted ? 0.0f : 1.0f);
}

void GameAudio::handle(const EncounterEvent& event, hue::Vec3 listener,
                       hue::Vec3 listener_right) noexcept {
    const auto index = static_cast<std::uint32_t>(event.type);
    if (!m_device || index >= kGameSoundCount || m_ids[index] == hue::audio::kInvalidSound)
        return;
    hue::audio::PlayParams params;
    params.gain = kSoundGain[index];
    if (kSpatial[index]) {
        const hue::Vec3 offset = event.position - listener;
        const float distance = hue::length(offset);
        if (std::isfinite(distance)) {
            params.gain *= 1.0f / (1.0f + 0.12f * distance);
            if (distance > 0.5f)
                params.pan = hue::clamp(hue::dot(offset * (1.0f / distance), listener_right),
                                        -1.0f, 1.0f) *
                             0.8f;
        }
        // A little per-actor detune keeps overlapping enemies from phasing.
        params.pitch = 1.0f + 0.04f * (static_cast<float>(event.actor % 3u) - 1.0f);
    }
    (void)m_mixer.play(m_ids[index], params);
}
