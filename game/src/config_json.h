// game/src/config_json.h
// Bounded, allocation-free JSON subset for flat game configuration schemas.
// No nesting, escapes or arbitrary paths: schema consumers whitelist every key.
#pragma once
#include "hue/core/result.h"
#include <cstddef>

inline constexpr std::size_t kConfigMaxBytes = 4096;
struct ConfigField {
    char key[48]{};
    char string[48]{};
    float numbers[4]{};
    unsigned count = 0;
    bool is_string = false;
    bool is_array = false;
};
struct ConfigObject {
    ConfigField fields[32]{};
    unsigned count = 0;
    const ConfigField* find(const char* key) const noexcept;
};
[[nodiscard]] hue::Result<ConfigObject> parse_config_json(const void* data, std::size_t size);
