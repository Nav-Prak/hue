// tests/core/test_string_id.cpp

#include <doctest/doctest.h>

#include "hue/core/string_id.h"

TEST_CASE("StringId: hashes are deterministic and distinguish engine names") {
    const auto first = hue::StringId::from_string("hit_begin");
    const auto second = hue::StringId::from_string("hit_begin");
    const auto different = hue::StringId::from_string("hit_end");
    CHECK(first == second);
    CHECK_FALSE(first == different);
    CHECK(first.value() != 0);
}

TEST_CASE("StringId: literal and runtime forms match") {
    using namespace hue::literals;
    constexpr auto literal = "cancel_window"_sid;
    constexpr auto runtime_form = hue::StringId::from_string("cancel_window");
    static_assert(literal == runtime_form);
    CHECK(literal == runtime_form);
}
