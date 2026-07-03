// tests/core/test_result.cpp

#include <doctest/doctest.h>

#include "hue/core/result.h"
#include "hue/core/version.h"

#include <cstring>
#include <string>

namespace {

hue::Result<int> parse_positive(int raw) {
    if (raw <= 0) {
        return hue::ErrorCode::kInvalidArgument;
    }
    return raw;
}

hue::Result<void> validate(bool ok) {
    if (!ok) {
        return hue::ErrorCode::kCorruptData;
    }
    return {};
}

} // namespace

TEST_CASE("Result holds a value") {
    auto r = parse_positive(42);
    REQUIRE(r.has_value());
    CHECK(r.value() == 42);
    CHECK(r.value_or(-1) == 42);
}

TEST_CASE("Result holds an error") {
    auto r = parse_positive(-5);
    REQUIRE(!r);
    CHECK(r.error() == hue::ErrorCode::kInvalidArgument);
    CHECK(r.value_or(-1) == -1);
}

TEST_CASE("Result with non-trivial payload moves correctly") {
    hue::Result<std::string> r{std::string("skeletal animation")};
    REQUIRE(r.has_value());
    hue::Result<std::string> moved{std::move(r)};
    CHECK(moved.value() == "skeletal animation");
}

TEST_CASE("Result<void> success and failure") {
    CHECK(validate(true).has_value());
    auto bad = validate(false);
    REQUIRE(!bad);
    CHECK(bad.error() == hue::ErrorCode::kCorruptData);
}

TEST_CASE("engine version matches project version") {
    auto v = hue::engine_version();
    CHECK(v.major == 0);
    CHECK(v.minor == 1);
    CHECK(std::strcmp(hue::engine_version_string(), "hue 0.1.0") == 0);
}
