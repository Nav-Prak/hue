// tests/audio/test_audio.cpp
#include "hue/audio/audio.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <doctest/doctest.h>
#include <limits>
#include <vector>

namespace {
using hue::audio::Mixer;
using hue::audio::parse_wav;
using hue::audio::PlayParams;
using hue::audio::SoundData;

void put16(std::vector<unsigned char>& out, unsigned value) {
    out.push_back(static_cast<unsigned char>(value & 0xffu));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xffu));
}
void put32(std::vector<unsigned char>& out, std::uint32_t value) {
    put16(out, value & 0xffffu);
    put16(out, value >> 16);
}
void tag(std::vector<unsigned char>& out, const char* id) {
    out.insert(out.end(), id, id + 4);
}
// A well-formed WAV; individual fields are corrupted by the tests below.
std::vector<unsigned char> wav(unsigned format, unsigned channels, std::uint32_t rate, unsigned bits,
                               const std::vector<unsigned char>& payload) {
    std::vector<unsigned char> out;
    tag(out, "RIFF");
    put32(out, static_cast<std::uint32_t>(4 + 8 + 16 + 8 + payload.size()));
    tag(out, "WAVE");
    tag(out, "fmt ");
    put32(out, 16);
    put16(out, format);
    put16(out, channels);
    put32(out, rate);
    put32(out, rate * channels * bits / 8);
    put16(out, channels * bits / 8);
    put16(out, bits);
    tag(out, "data");
    put32(out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}
std::vector<unsigned char> pcm(std::initializer_list<int> samples) {
    std::vector<unsigned char> out;
    for (const int sample : samples)
        put16(out, static_cast<unsigned>(sample) & 0xffffu);
    return out;
}
SoundData constant(float value, std::size_t frames, std::uint32_t rate = hue::audio::kOutputRate) {
    SoundData sound;
    sound.sample_rate = rate;
    for (std::size_t i = 0; i < frames; ++i)
        REQUIRE(sound.samples.push_back(value));
    return sound;
}
std::vector<unsigned char> file_bytes(const char* name) {
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/%s.wav", HUE_TEST_AUDIO_DIR, name);
    auto* file = std::fopen(path, "rb");
    REQUIRE(file != nullptr);
    std::vector<unsigned char> bytes(hue::audio::kMaxWavBytes);
    bytes.resize(std::fread(bytes.data(), 1, bytes.size(), file));
    std::fclose(file);
    return bytes;
}
} // namespace

TEST_CASE("wav: PCM16 mono and stereo decode to bounded mono floats") {
    const auto mono = wav(1, 1, 22050, 16, pcm({0, 16384, -32768, 32767}));
    auto parsed = parse_wav(mono.data(), mono.size());
    REQUIRE(parsed);
    CHECK(parsed.value().sample_rate == 22050);
    REQUIRE(parsed.value().samples.size() == 4);
    CHECK(parsed.value().samples[0] == 0.0f);
    CHECK(parsed.value().samples[1] == doctest::Approx(0.5f));
    CHECK(parsed.value().samples[2] == -1.0f);
    CHECK(parsed.value().samples[3] < 1.0f);
    const auto stereo = wav(1, 2, 48000, 16, pcm({16384, 0, -16384, -16384}));
    auto averaged = parse_wav(stereo.data(), stereo.size());
    REQUIRE(averaged);
    REQUIRE(averaged.value().samples.size() == 2);
    CHECK(averaged.value().samples[0] == doctest::Approx(0.25f)); // channels averaged
    CHECK(averaged.value().samples[1] == doctest::Approx(-0.5f));
}

TEST_CASE("wav: float32 decodes, clamps, and rejects non-finite samples") {
    const auto encode = [](std::initializer_list<float> values) {
        std::vector<unsigned char> out;
        for (const float value : values) {
            std::uint32_t raw = 0;
            std::memcpy(&raw, &value, sizeof(raw));
            put32(out, raw);
        }
        return out;
    };
    const auto good = wav(3, 1, 44100, 32, encode({0.25f, -3.0f, 7.0f}));
    auto parsed = parse_wav(good.data(), good.size());
    REQUIRE(parsed);
    CHECK(parsed.value().samples[0] == 0.25f);
    CHECK(parsed.value().samples[1] == -1.0f);
    CHECK(parsed.value().samples[2] == 1.0f);
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity()}) {
        const auto file = wav(3, 1, 44100, 32, encode({0.0f, bad}));
        CHECK_FALSE(parse_wav(file.data(), file.size()));
    }
}

TEST_CASE("wav: every truncation and unsupported or inconsistent header is rejected") {
    const auto good = wav(1, 1, 22050, 16, pcm({1, 2, 3, 4, 5, 6}));
    REQUIRE(parse_wav(good.data(), good.size()));
    for (std::size_t size = 0; size < good.size(); ++size)
        CHECK_FALSE(parse_wav(good.data(), size));
    CHECK_FALSE(parse_wav(nullptr, good.size()));
    CHECK_FALSE(parse_wav(good.data(), hue::audio::kMaxWavBytes + 1));
    // Trailing bytes after the RIFF body are ignored, not read.
    auto padded = good;
    padded.insert(padded.end(), 64, 0xEE);
    CHECK(parse_wav(padded.data(), padded.size()));

    const auto payload = pcm({1, 2, 3, 4});
    CHECK_FALSE(parse_wav(wav(1, 0, 22050, 16, payload).data(), 44 + payload.size())); // no channels
    for (const auto& file :
         {wav(1, 3, 22050, 16, pcm({1, 2, 3})),   // three channels
          wav(1, 1, 7999, 16, payload),           // rate too low
          wav(1, 1, 96000, 16, payload),          // rate too high
          wav(1, 1, 22050, 8, payload),           // 8-bit PCM
          wav(1, 1, 22050, 24, pcm({1, 2, 3})),   // 24-bit PCM
          wav(3, 1, 22050, 16, payload),          // 16-bit "float"
          wav(2, 1, 22050, 16, payload),          // ADPCM
          wav(0xFFFE, 1, 22050, 16, payload),     // extensible
          wav(1, 2, 22050, 16, pcm({1, 2, 3})),   // payload not a whole frame
          wav(1, 1, 22050, 16, {})}) {            // empty payload
        CHECK_FALSE(parse_wav(file.data(), file.size()));
    }
    const auto corrupt = [&good](std::size_t offset, std::uint32_t value) {
        auto file = good;
        std::memcpy(file.data() + offset, &value, 4);
        return file;
    };
    for (const auto& file :
         {corrupt(0, 0x46464952u + 1),  // not RIFF
          corrupt(8, 0),                // not WAVE
          corrupt(4, 0xFFFFFFFFu),      // RIFF size beyond the buffer
          corrupt(4, 2),                // RIFF size too small to hold WAVE
          corrupt(16, 0xFFFFFFF0u),     // fmt chunk size overflows
          corrupt(16, 8),               // fmt chunk too short
          corrupt(40, 0xFFFFFFFFu),     // data size beyond the buffer
          corrupt(40, 13),              // data size beyond the buffer by one
          corrupt(12, 0x20746D66u + 1), // no fmt chunk at all
          corrupt(36, 0x61746164u + 1)}) { // no data chunk at all
        CHECK_FALSE(parse_wav(file.data(), file.size()));
    }
    auto wrong_align = good;
    wrong_align[32] = 4; // block align disagrees with channels * bits
    CHECK_FALSE(parse_wav(wrong_align.data(), wrong_align.size()));
    // data before fmt, and a second data chunk
    std::vector<unsigned char> reordered(good.begin(), good.begin() + 12);
    reordered.insert(reordered.end(), good.begin() + 36, good.end());
    reordered.insert(reordered.end(), good.begin() + 12, good.begin() + 36);
    CHECK_FALSE(parse_wav(reordered.data(), reordered.size()));
    auto doubled = good;
    doubled.insert(doubled.end(), good.begin() + 36, good.end());
    const auto riff = static_cast<std::uint32_t>(doubled.size() - 8);
    std::memcpy(doubled.data() + 4, &riff, 4);
    CHECK_FALSE(parse_wav(doubled.data(), doubled.size()));
}

TEST_CASE("wav: unknown chunks are skipped and the chunk count is capped") {
    const auto build = [](unsigned extra_chunks) {
        std::vector<unsigned char> out;
        tag(out, "RIFF");
        put32(out, 0);
        tag(out, "WAVE");
        for (unsigned i = 0; i < extra_chunks; ++i) {
            tag(out, "LIST");
            put32(out, 3); // odd size: one pad byte follows
            out.insert(out.end(), {1, 2, 3, 0});
        }
        const auto tail = wav(1, 1, 22050, 16, pcm({100, -100}));
        out.insert(out.end(), tail.begin() + 12, tail.end());
        const auto riff = static_cast<std::uint32_t>(out.size() - 8);
        std::memcpy(out.data() + 4, &riff, 4);
        return out;
    };
    const auto few = build(8);
    auto parsed = parse_wav(few.data(), few.size());
    REQUIRE(parsed);
    CHECK(parsed.value().samples.size() == 2);
    const auto many = build(hue::audio::kMaxWavChunks);
    CHECK_FALSE(parse_wav(many.data(), many.size()));
}

TEST_CASE("wav: the shipped sound effects all parse") {
    for (const char* name :
         {"fight_start", "swing_light", "swing_heavy", "hit_light", "hit_heavy", "stagger", "dodge",
          "death", "lock_on", "lock_off", "victory", "defeat"}) {
        INFO(name);
        const auto bytes = file_bytes(name);
        auto parsed = parse_wav(bytes.data(), bytes.size());
        REQUIRE(parsed);
        CHECK(parsed.value().sample_rate == 22050);
        CHECK(parsed.value().samples.size() > 1000);
        CHECK(parsed.value().samples.size() < 22050 * 3);
    }
}

TEST_CASE("mixer: silence, playback, panning and resampling") {
    Mixer mixer;
    const auto loud = mixer.add_sound(constant(0.5f, 480));
    const auto slow = mixer.add_sound(constant(0.5f, 240, 24000)); // half the output rate
    REQUIRE(loud);
    REQUIRE(slow);
    mixer.seal();
    std::vector<float> out(2 * 1024, 9.0f);
    mixer.mix(out.data(), 1024);
    for (const float sample : out)
        CHECK(sample == 0.0f);
    CHECK(mixer.active_voices() == 0);
    CHECK(mixer.frames_mixed() == 1024);

    PlayParams left;
    left.pan = -1.0f;
    REQUIRE(mixer.play(loud.value(), left));
    mixer.mix(out.data(), 1024);
    CHECK(out[0] == doctest::Approx(0.5f));          // hard left
    CHECK(std::abs(out[1]) < 1.0e-6f);
    CHECK(out[2 * 479] == doctest::Approx(0.5f));    // last source frame
    CHECK(out[2 * 480] == 0.0f);                     // one-shot ended
    CHECK(mixer.active_voices() == 0);

    REQUIRE(mixer.play(loud.value())); // centered: equal power
    mixer.mix(out.data(), 16);
    CHECK(out[0] == doctest::Approx(0.5f * std::sqrt(0.5f)));
    CHECK(out[1] == doctest::Approx(out[0]));
    mixer.stop_all();
    mixer.mix(out.data(), 16);
    CHECK(out[0] == 0.0f);

    PlayParams right;
    right.pan = 1.0f;
    REQUIRE(mixer.play(slow.value(), right));
    mixer.mix(out.data(), 1024);
    CHECK(out[1] == doctest::Approx(0.5f));
    CHECK(out[2 * 478 + 1] == doctest::Approx(0.5f)); // 240 frames at 24 kHz last 480 output frames
    CHECK(out[2 * 481 + 1] == 0.0f);
}

TEST_CASE("mixer: output stays finite and within range under overload and bad input") {
    Mixer mixer;
    const auto loud = mixer.add_sound(constant(1.0f, 48000));
    SoundData hostile;
    hostile.sample_rate = 48000;
    for (const float value : {std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity(), -50.0f, 0.5f})
        REQUIRE(hostile.samples.push_back(value));
    const auto odd = mixer.add_sound(hostile);
    REQUIRE(loud);
    REQUIRE(odd);
    // Rejected registrations: empty, bad rate, and anything after sealing.
    CHECK_FALSE(mixer.add_sound(SoundData{}));
    CHECK_FALSE(mixer.add_sound(constant(0.1f, 8, 4000)));
    mixer.seal();
    CHECK_FALSE(mixer.add_sound(constant(0.1f, 8)));
    CHECK(mixer.sound_count() == 2);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    PlayParams bad;
    bad.gain = nan;
    CHECK_FALSE(mixer.play(loud.value(), bad));
    bad = {};
    bad.pan = std::numeric_limits<float>::infinity();
    CHECK_FALSE(mixer.play(loud.value(), bad));
    CHECK_FALSE(mixer.play(99));
    CHECK_FALSE(mixer.play(hue::audio::kInvalidSound));
    CHECK(mixer.dropped_commands() == 4);

    // Far more plays than voices or ring slots, all at maximum gain.
    PlayParams huge;
    huge.gain = 1000.0f;
    huge.pitch = 1000.0f;
    std::uint32_t accepted = 0;
    for (int i = 0; i < 500; ++i)
        accepted += mixer.play(i % 2 == 0 ? loud.value() : odd.value(), huge) ? 1u : 0u;
    CHECK(accepted == hue::audio::kCommandCapacity);
    CHECK(mixer.dropped_commands() == 4 + 500 - hue::audio::kCommandCapacity);
    std::vector<float> out(2 * 4096);
    for (int block = 0; block < 8; ++block) {
        mixer.mix(out.data(), 4096);
        CHECK(mixer.active_voices() <= hue::audio::kMaxVoices);
        for (const float sample : out) {
            REQUIRE(std::isfinite(sample));
            REQUIRE(sample >= -1.0f);
            REQUIRE(sample <= 1.0f);
        }
        for (int i = 0; i < 40; ++i)
            (void)mixer.play(loud.value(), huge);
    }
    mixer.set_master_gain(nan);
    mixer.mix(out.data(), 64);
    for (std::size_t i = 0; i < 128; ++i)
        CHECK(out[i] == 0.0f);
    mixer.mix(nullptr, 64); // ignored
    mixer.mix(out.data(), 0);
}

TEST_CASE("mixer: loops continue, are never stolen, and stop on request") {
    Mixer mixer;
    const auto bed = mixer.add_sound(constant(0.1f, 100));
    const auto shot = mixer.add_sound(constant(0.1f, 100000));
    REQUIRE(bed);
    REQUIRE(shot);
    mixer.seal();
    PlayParams loop;
    loop.loop = true;
    loop.pan = -1.0f;
    REQUIRE(mixer.play(bed.value(), loop));
    std::vector<float> out(2 * 1000);
    mixer.mix(out.data(), 1000);
    CHECK(out[2 * 999] == doctest::Approx(0.1f)); // ten times the sound's length later
    for (std::uint32_t i = 0; i < hue::audio::kMaxVoices * 3; ++i) {
        PlayParams one;
        one.pan = 1.0f;
        REQUIRE(mixer.play(shot.value(), one));
        mixer.mix(out.data(), 8);
    }
    CHECK(mixer.active_voices() == hue::audio::kMaxVoices);
    CHECK(out[0] == doctest::Approx(0.1f)); // the loop is still on the left
    REQUIRE(mixer.stop_all());
    mixer.mix(out.data(), 8);
    CHECK(mixer.active_voices() == 0);
    CHECK(out[0] == 0.0f);
}
