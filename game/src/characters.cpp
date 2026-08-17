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

hue::Result<hue::asset::StaticMeshData> bind_pose_preview(hue::asset::SkinnedMeshData&& skinned) {
    hue::asset::StaticMeshData preview;

    if (!preview.vertices.reserve(skinned.vertices.size())) {
        return hue::ErrorCode::kOutOfMemory;
    }
    for (std::size_t v = 0; v < skinned.vertices.size(); ++v) {
        hue::asset::StaticVertex vertex;
        vertex.position = skinned.vertices[v].position;
        vertex.normal = skinned.vertices[v].normal;
        vertex.uv = skinned.vertices[v].uv;
        if (!preview.vertices.push_back(vertex)) {
            return hue::ErrorCode::kOutOfMemory;
        }
    }
    if (!preview.indices.append(skinned.indices.data(), skinned.indices.size())) {
        return hue::ErrorCode::kOutOfMemory;
    }
    for (std::size_t p = 0; p < skinned.primitives.size(); ++p) {
        if (!preview.primitives.push_back(skinned.primitives[p])) {
            return hue::ErrorCode::kOutOfMemory;
        }
        hue::asset::MeshInstance instance;
        instance.primitive_index = static_cast<std::uint32_t>(p);
        instance.transform = hue::Mat4::identity();
        if (!preview.instances.push_back(instance)) {
            return hue::ErrorCode::kOutOfMemory;
        }
    }
    for (std::size_t t = 0; t < skinned.textures.size(); ++t) {
        if (!preview.textures.push_back(std::move(skinned.textures[t]))) {
            return hue::ErrorCode::kOutOfMemory;
        }
    }
    for (std::size_t m = 0; m < skinned.materials.size(); ++m) {
        if (!preview.materials.push_back(skinned.materials[m])) {
            return hue::ErrorCode::kOutOfMemory;
        }
    }
    preview.bounds = skinned.bounds;
    return preview;
}
