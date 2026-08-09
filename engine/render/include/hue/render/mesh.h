// engine/render/include/hue/render/mesh.h
//
// Handle to a GPU-resident static mesh. Generation counter catches
// use-after-destroy: a stale handle never aliases a reused slot.

#pragma once

#include <cstdint>

namespace hue {

struct MeshHandle {
    std::uint32_t slot = UINT32_MAX;
    std::uint32_t generation = 0;

    [[nodiscard]] bool valid() const noexcept { return slot != UINT32_MAX; }
};

} // namespace hue
