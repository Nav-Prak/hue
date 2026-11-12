// game/src/game_audio.h
// Maps encounter events to the fixed set of shipped sound effects. Sounds are
// loaded by whitelisted name from <exe dir>/assets/audio through the bounded
// WAV parser; a missing or rejected file leaves that one effect silent and
// the game running. Audio is presentation only: it never feeds back into
// gameplay, and the game is fully playable without an output device.
#pragma once

#include "encounter.h"
#include "hue/audio/audio.h"

#include <cstdint>
#include <optional>

inline constexpr std::uint32_t kGameSoundCount = 12;

class GameAudio {
public:
    // Returns how many of the kGameSoundCount effects loaded.
    std::uint32_t load();
    // Opens the output device. False when the machine has none.
    bool open_device();
    // Plays the effect for one event, attenuated and panned relative to the
    // listener. No-op without a device.
    void handle(const EncounterEvent& event, hue::Vec3 listener, hue::Vec3 listener_right) noexcept;
    void set_muted(bool muted) noexcept;
    [[nodiscard]] bool muted() const noexcept { return m_muted; }
    [[nodiscard]] bool has_device() const noexcept { return m_device.has_value(); }
    [[nodiscard]] std::uint32_t loaded() const noexcept { return m_loaded; }
    [[nodiscard]] const hue::audio::Mixer& mixer() const noexcept { return m_mixer; }

private:
    hue::audio::Mixer m_mixer;                   // must outlive the device
    std::optional<hue::audio::Device> m_device;  // destroyed first
    std::uint32_t m_ids[kGameSoundCount]{};
    std::uint32_t m_loaded = 0;
    bool m_muted = false;
};
