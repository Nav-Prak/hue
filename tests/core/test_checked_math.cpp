// tests/core/test_checked_math.cpp

#include <doctest/doctest.h>

#include "hue/core/checked_math.h"

#include <cstddef>
#include <limits>

TEST_CASE("checked math: add accepts valid sizes and rejects overflow") {
    std::size_t result = 0;
    CHECK(hue::checked_add(40, 2, result));
    CHECK(result == 42);
    CHECK_FALSE(hue::checked_add(std::numeric_limits<std::size_t>::max(), 1, result));
}

TEST_CASE("checked math: multiply accepts valid sizes and rejects overflow") {
    std::size_t result = 0;
    CHECK(hue::checked_mul(6, 7, result));
    CHECK(result == 42);
    CHECK(hue::checked_mul(0, std::numeric_limits<std::size_t>::max(), result));
    CHECK(result == 0);
    CHECK_FALSE(hue::checked_mul(std::numeric_limits<std::size_t>::max(), 2, result));
}

TEST_CASE("checked math: alignment is power-of-two and overflow safe") {
    std::size_t result = 0;
    CHECK(hue::checked_align_up(65, 64, result));
    CHECK(result == 128);
    CHECK_FALSE(hue::checked_align_up(10, 3, result));
    CHECK_FALSE(hue::checked_align_up(std::numeric_limits<std::size_t>::max(), 8, result));
}
