// tools/fuzz/fuzz_wav.cpp
// Memory-only WAV parsing followed by mixing whatever was accepted, so the
// decoded table is exercised the way the audio thread would read it.
#include "hue/audio/audio.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    auto parsed = hue::audio::parse_wav(data, size);
    if (!parsed)
        return 0;
    const auto& sound = parsed.value();
    if (sound.samples.empty() || sound.samples.size() > hue::audio::kMaxSoundFrames ||
        sound.sample_rate < hue::audio::kMinSampleRate ||
        sound.sample_rate > hue::audio::kMaxSampleRate)
        std::abort();
    for (std::size_t i = 0; i < sound.samples.size(); ++i)
        if (!(sound.samples[i] >= -1.0f && sound.samples[i] <= 1.0f))
            std::abort();
    hue::audio::Mixer mixer;
    const auto id = mixer.add_sound(sound);
    if (!id)
        std::abort();
    mixer.seal();
    hue::audio::PlayParams params;
    params.pitch = 4.0f;
    params.loop = size % 2 == 0;
    (void)mixer.play(id.value(), params);
    float out[2 * 256];
    for (int block = 0; block < 4; ++block) {
        mixer.mix(out, 256);
        for (const float sample : out)
            if (!std::isfinite(sample) || sample < -1.0f || sample > 1.0f)
                std::abort();
    }
    return 0;
}
