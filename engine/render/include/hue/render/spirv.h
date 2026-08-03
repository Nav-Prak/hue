// engine/render/include/hue/render/spirv.h
//
// SPIR-V byte-stream validation for runtime shader loading. Shader files
// are untrusted input at the reload hook (an agent or human can drop
// arbitrary bytes in the shaders directory), so the loader checks shape
// before the bytes reach the driver. Full semantic validation is the
// build-time spirv-val gate; this is the runtime boundary check.
// Pure function: unit-tested directly and fuzzable in Week 14.

#pragma once

#include <cstddef>

#include "hue/core/result.h"

namespace hue {

inline constexpr std::size_t kSpirvMaxBytes = 8 * 1024 * 1024; // no engine shader is near this

// Checks size bounds (non-empty, multiple of 4, <= kSpirvMaxBytes) and the
// SPIR-V magic number in the declared endianness.
[[nodiscard]] Result<void> validate_spirv_bytes(const void* bytes, std::size_t size) noexcept;

} // namespace hue
