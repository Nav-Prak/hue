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
