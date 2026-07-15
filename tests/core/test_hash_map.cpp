// tests/core/test_hash_map.cpp

#include <doctest/doctest.h>

#include "hue/core/hash_map.h"
#include "hue/core/string_id.h"

TEST_CASE("HashMap: inserts, grows, finds, and replaces values") {
    hue::HashMap<int, int> map;
    for (int i = 0; i < 200; ++i) {
        REQUIRE(map.insert(i, i * 2));
    }
    CHECK(map.size() == 200);
    for (int i = 0; i < 200; ++i) {
        const int* value = map.find(i);
        REQUIRE(value != nullptr);
        CHECK(*value == i * 2);
    }

    REQUIRE(map.insert(42, 9001));
    REQUIRE(map.find(42) != nullptr);
    CHECK(*map.find(42) == 9001);
    CHECK(map.size() == 200);
}

TEST_CASE("HashMap: erase preserves neighboring probe chains") {
    hue::HashMap<int, int> map;
    REQUIRE(map.reserve(128));
    for (int i = 0; i < 80; ++i) {
        REQUIRE(map.insert(i, i));
    }
    for (int i = 0; i < 80; i += 2) {
        CHECK(map.erase(i));
    }
    CHECK(map.size() == 40);
    for (int i = 1; i < 80; i += 2) {
        REQUIRE(map.find(i) != nullptr);
        CHECK(*map.find(i) == i);
    }
    CHECK_FALSE(map.erase(1000));
}

TEST_CASE("HashMap: StringId keys use their stable hash") {
    hue::HashMap<hue::StringId, int> map;
    const auto health = hue::StringId::from_string("health");
    const auto poise = hue::StringId::from_string("poise");
    REQUIRE(map.insert(health, 100));
    REQUIRE(map.insert(poise, 50));
    REQUIRE(map.find(health) != nullptr);
    CHECK(*map.find(health) == 100);
    CHECK(map.find(hue::StringId::from_string("missing")) == nullptr);
}
