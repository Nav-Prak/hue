// engine/core/include/hue/core/checked_math.h
//
// Overflow-safe size arithmetic for every allocation and serialized-size
// calculation. Callers must check the boolean result before using `out`.

#pragma once

#include <cstddef>
#include <limits>

namespace hue {

[[nodiscard]] constexpr bool checked_add(std::size_t left, std::size_t right,
                                         std::size_t& out) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    out = left + right;
    return true;
}

[[nodiscard]] constexpr bool checked_mul(std::size_t left, std::size_t right,
                                         std::size_t& out) noexcept {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    out = left * right;
    return true;
}

[[nodiscard]] constexpr bool checked_align_up(std::size_t value, std::size_t alignment,
                                              std::size_t& out) noexcept {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return false;
    }
    std::size_t with_padding = 0;
    if (!checked_add(value, alignment - 1, with_padding)) {
        return false;
    }
    out = with_padding & ~(alignment - 1);
    return true;
}

} // namespace hue
