// engine/core/include/hue/core/version.h
//
// Engine version, single point of truth fed from CMake's project() version.

#pragma once

#include <cstdint>

namespace hue {

struct Version {
    std::uint32_t major;
    std::uint32_t minor;
    std::uint32_t patch;
};

Version engine_version();

// "hue 0.1.0" — static storage, never freed.
const char* engine_version_string();

} // namespace hue
