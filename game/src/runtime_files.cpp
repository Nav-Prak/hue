// game/src/runtime_files.cpp
#include "runtime_files.h"
#include "scene_config.h"
#include <cstdio>
#include <cstring>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif
bool runtime_path(char* out, std::size_t capacity, const char* suffix) noexcept {
    if (!out || capacity < 2 || capacity > 32768 || !suffix)
        return false;
#if defined(_WIN32)
    const auto count = GetModuleFileNameA(nullptr, out, static_cast<DWORD>(capacity));
    if (!count || count >= capacity)
        return false;
#else
    const auto count = readlink("/proc/self/exe", out, capacity - 1);
    if (count <= 0 || static_cast<std::size_t>(count) >= capacity - 1)
        return false;
    out[count] = 0;
#endif
    for (char* c = out; *c; ++c)
        if (*c == '\\')
            *c = '/';
    char* slash = std::strrchr(out, '/');
    if (!slash)
        return false;
    const auto used = static_cast<std::size_t>(slash - out) + 1;
    const int written = std::snprintf(out + used, capacity - used, "%s", suffix);
    return written >= 0 && static_cast<std::size_t>(written) < capacity - used;
}
hue::Result<std::size_t> read_bounded_file(const char* path, char* buffer, std::size_t capacity) {
    if (!path || !buffer || capacity == 0)
        return hue::ErrorCode::kInvalidArgument;
    auto* file = std::fopen(path, "rb");
    if (!file)
        return hue::ErrorCode::kNotFound;
    const std::size_t size = std::fread(buffer, 1, capacity, file);
    const bool bad = std::ferror(file) != 0 || std::fgetc(file) != EOF;
    const int closed = std::fclose(file);
    if (bad || closed || size == 0)
        return hue::ErrorCode::kCorruptData;
    return size;
}
hue::Result<ConfigBytes> read_config_file(const char* path) {
    ConfigBytes result;
    const auto size = read_bounded_file(path, result.data, sizeof(result.data));
    if (!size)
        return size.error();
    result.size = size.value();
    return result;
}
hue::Result<SceneBytes> read_scene_file(const char* scene_name) {
    if (!valid_scene_name(scene_name))
        return hue::ErrorCode::kInvalidArgument;
    char relative[64];
    std::snprintf(relative, sizeof(relative), "assets/config/%s.json", scene_name);
    char path[1024];
    if (!runtime_path(path, sizeof(path), relative))
        return hue::ErrorCode::kInvalidArgument;
    SceneBytes result;
    const auto size = read_bounded_file(path, result.data, sizeof(result.data));
    if (!size)
        return size.error();
    result.size = size.value();
    return result;
}
