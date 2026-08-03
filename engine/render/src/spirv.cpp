// engine/render/src/spirv.cpp

#include "hue/render/spirv.h"

#include <cstdint>
#include <cstring>

namespace hue {

Result<void> validate_spirv_bytes(const void* bytes, std::size_t size) noexcept {
    if (bytes == nullptr || size == 0) {
        return ErrorCode::kCorruptData;
    }
    if (size % 4 != 0 || size < 20) { // header alone is 5 words
        return ErrorCode::kCorruptData;
    }
    if (size > kSpirvMaxBytes) {
        return ErrorCode::kCorruptData;
    }

    std::uint32_t magic = 0;
    std::memcpy(&magic, bytes, sizeof(magic));
    constexpr std::uint32_t kSpirvMagic = 0x07230203u;
    constexpr std::uint32_t kSpirvMagicSwapped = 0x03022307u;
    if (magic != kSpirvMagic && magic != kSpirvMagicSwapped) {
        return ErrorCode::kCorruptData;
    }
    return {};
}

} // namespace hue
