// tests/core/test_array.cpp

#include <doctest/doctest.h>

#include "hue/core/array.h"

#include <cstddef>

TEST_CASE("Array: grows contiguously and preserves values") {
    hue::Array<int> values;
    for (int i = 0; i < 100; ++i) {
        REQUIRE(values.push_back(i * 3));
    }
    REQUIRE(values.size() == 100);
    CHECK(values.capacity() >= values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        CHECK(values[i] == static_cast<int>(i) * 3);
    }
}

TEST_CASE("Array: at bounds-checks and pop destroys from the end") {
    hue::Array<int> values;
    REQUIRE(values.push_back(10));
    REQUIRE(values.push_back(20));
    REQUIRE(values.at(1) != nullptr);
    CHECK(*values.at(1) == 20);
    CHECK(values.at(2) == nullptr);
    CHECK(values.pop_back());
    CHECK(values.size() == 1);
    CHECK(values.pop_back());
    CHECK_FALSE(values.pop_back());
}

TEST_CASE("Array: reserve overflow fails without changing storage") {
    hue::Array<int> values;
    auto reserved = values.reserve(static_cast<std::size_t>(-1));
    REQUIRE_FALSE(reserved);
    CHECK(reserved.error() == hue::ErrorCode::kOutOfMemory);
    CHECK(values.empty());
}
