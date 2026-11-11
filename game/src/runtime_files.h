// game/src/runtime_files.h
// Small bounded files and executable-relative paths; never fall back to source paths.
#pragma once
#include "config_json.h"
struct ConfigBytes {
    char data[kConfigMaxBytes];
    std::size_t size = 0;
};
[[nodiscard]] bool runtime_path(char* out, std::size_t capacity, const char* suffix) noexcept;
[[nodiscard]] hue::Result<ConfigBytes> read_config_file(const char* path);
// Reads a whole file of 1..capacity bytes; larger, empty or unreadable files fail.
[[nodiscard]] hue::Result<std::size_t> read_bounded_file(const char* path, char* buffer,
                                                         std::size_t capacity);
struct SceneBytes {
    char data[kDocumentMaxBytes];
    std::size_t size = 0;
};
// Resolves an approved scene name to <exe dir>/assets/config/<name>.json and
// reads it. The name is validated first; it can never contain a path.
[[nodiscard]] hue::Result<SceneBytes> read_scene_file(const char* scene_name);
