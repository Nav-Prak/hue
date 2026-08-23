// game/src/characters.cpp

#include "characters.h"

#include "hue/asset/gltf_loader.h"
#include "hue/core/log.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

// Assets stage next to the executable (like shaders); the working
// directory of tests/CI is anything but stable.
void executable_relative(char* buffer, std::size_t size, const char* suffix) {
#if defined(_WIN32)
    const DWORD length = GetModuleFileNameA(nullptr, buffer, static_cast<DWORD>(size));
    buffer[(length < size) ? length : size - 1] = '\0';
    for (char* c = buffer; *c != '\0'; ++c) {
        if (*c == '\\') {
            *c = '/';
        }
    }
#else
    const ssize_t length = readlink("/proc/self/exe", buffer, size - 1);
    buffer[(length > 0) ? static_cast<std::size_t>(length) : 0] = '\0';
#endif
    char* last_slash = std::strrchr(buffer, '/');
    if (last_slash != nullptr) {
        *last_slash = '\0';
    }
    const std::size_t used = std::strlen(buffer);
    std::snprintf(buffer + used, size - used, "/%s", suffix);
}

} // namespace

hue::Result<hue::asset::SkinnedMeshData> load_character(const char* file_name) {
    char path[512];
    std::snprintf(path, sizeof(path), "assets/models/%s", file_name);
    char full_path[1024];
    executable_relative(full_path, sizeof(full_path), path);

    auto skinned = hue::asset::load_gltf_skinned_file(full_path);
    if (!skinned) {
        HUE_LOG_WARN("character import failed: %s", full_path);
        return skinned.error();
    }

    const hue::asset::SkinnedMeshData& data = skinned.value();
    HUE_LOG_INFO("character %s: %zu joints, %zu clips, %zu vertices", file_name,
                 data.joints.size(), data.clips.size(), data.vertices.size());
    for (std::size_t c = 0; c < data.clips.size(); ++c) {
        HUE_LOG_INFO("  clip '%s': %.2fs, %zu channels", data.clips[c].name,
                     static_cast<double>(data.clips[c].duration),
                     data.clips[c].channels.size());
    }
    return skinned;
}

hue::Result<hue::anim::AnimationEventTrack> load_character_events(const char* file_name) {
    char path[512];
    std::snprintf(path, sizeof(path), "assets/models/%s", file_name);
    char full_path[1024];
    executable_relative(full_path, sizeof(full_path), path);
    auto events = hue::anim::load_animation_events_file(full_path);
    if (!events) {
        HUE_LOG_WARN("animation event track failed: %s", full_path);
    }
    return events;
}
