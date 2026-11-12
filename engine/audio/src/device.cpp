// engine/audio/src/device.cpp
//
// Playback device: 48 kHz stereo float, driven by Mixer::mix on the
// platform's audio thread. The same feature macros as miniaudio_impl.cpp
// keep the declarations consistent.

#include "hue/audio/audio.h"

#include "hue/core/log.h"

#include <new>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include <miniaudio.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace hue::audio {

namespace {
void data_callback(ma_device* device, void* output, const void*, ma_uint32 frame_count) {
    // miniaudio hands over a buffer of exactly frame_count stereo float frames.
    static_cast<Mixer*>(device->pUserData)->mix(static_cast<float*>(output), frame_count);
}
} // namespace

Result<Device> Device::create(Mixer& mixer) noexcept {
    auto memory = heap_allocate(sizeof(ma_device), alignof(ma_device), MemoryTag::kCore);
    if (!memory) {
        return memory.error();
    }
    auto* device = static_cast<ma_device*>(memory.value());

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = kOutputRate;
    config.dataCallback = &data_callback;
    config.pUserData = &mixer;

    mixer.seal();
    if (ma_device_init(nullptr, &config, device) != MA_SUCCESS) {
        (void)heap_free(device);
        HUE_LOG_WARN("audio: no usable output device");
        return ErrorCode::kUnsupported;
    }
    if (ma_device_start(device) != MA_SUCCESS) {
        ma_device_uninit(device);
        (void)heap_free(device);
        HUE_LOG_WARN("audio: output device failed to start");
        return ErrorCode::kUnsupported;
    }
    HUE_LOG_INFO("audio device: %s, %u Hz stereo", device->playback.name, device->sampleRate);
    Device result;
    result.m_device = device;
    return result;
}

Device::Device(Device&& other) noexcept : m_device(other.m_device) {
    other.m_device = nullptr;
}

Device::~Device() {
    if (m_device != nullptr) {
        auto* device = static_cast<ma_device*>(m_device);
        ma_device_uninit(device); // stops and joins the audio thread
        (void)heap_free(device);
        m_device = nullptr;
    }
}

} // namespace hue::audio
