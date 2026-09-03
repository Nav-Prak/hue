#include <doctest/doctest.h>

#include "hue/physics/physics.h"

namespace {

constexpr float kTick = 1.0f / 60.0f;

// Ground box: top surface at y = 0.
void add_ground(hue::physics::PhysicsWorld& world) {
    REQUIRE(world.add_static_box({0.0f, -0.5f, 0.0f}, hue::Quat{}, {20.0f, 0.5f, 20.0f}));
}

} // namespace

TEST_CASE("physics: character falls under gravity and lands on the arena floor") {
    auto world = hue::physics::PhysicsWorld::create();
    REQUIRE(world);
    add_ground(world.value());

    hue::physics::CharacterDesc desc;
    desc.position = {0.0f, 3.0f, 0.0f};
    auto character = world.value().create_character(desc);
    REQUIRE(character);
    CHECK_FALSE(world.value().character_grounded(character.value()));

    for (int i = 0; i < 120; ++i) { // two seconds is plenty to fall 3m
        REQUIRE(world.value().update(kTick));
    }
    CHECK(world.value().character_grounded(character.value()));
    const hue::Vec3 position = world.value().character_position(character.value());
    CHECK(position.y == doctest::Approx(0.0f).epsilon(0.05f));
}

TEST_CASE("physics: walking moves the character and walls block it") {
    auto world = hue::physics::PhysicsWorld::create();
    REQUIRE(world);
    add_ground(world.value());
    // Wall ahead: near face at x = 2.0.
    REQUIRE(world.value().add_static_box({2.5f, 2.0f, 0.0f}, hue::Quat{}, {0.5f, 2.0f, 4.0f}));

    hue::physics::CharacterDesc desc;
    desc.position = {0.0f, 0.1f, 0.0f};
    auto character = world.value().create_character(desc);
    REQUIRE(character);

    // Walk +x at 2 m/s for 3 seconds: unobstructed distance would be 6m,
    // but the wall at x=2 must stop the capsule (radius 0.35) short of it.
    for (int i = 0; i < 180; ++i) {
        world.value().set_character_velocity(character.value(), {2.0f, 0.0f, 0.0f});
        REQUIRE(world.value().update(kTick));
    }
    const hue::Vec3 position = world.value().character_position(character.value());
    CHECK(position.x > 1.0f);            // it did travel
    CHECK(position.x < 2.0f);            // but never inside the wall
    CHECK(position.x == doctest::Approx(2.0f - desc.radius).epsilon(0.05f));
    CHECK(world.value().character_grounded(character.value()));
}

TEST_CASE("physics: walkable slope keeps the character grounded while climbing") {
    auto world = hue::physics::PhysicsWorld::create();
    REQUIRE(world);
    add_ground(world.value());
    // 25 degree ramp along +x (rotation about z tilts the box top toward -x).
    const hue::Quat tilt = hue::Quat::from_axis_angle({0.0f, 0.0f, 1.0f}, hue::radians(25.0f));
    REQUIRE(world.value().add_static_box({3.0f, 0.0f, 0.0f}, tilt, {3.0f, 0.5f, 3.0f}));

    hue::physics::CharacterDesc desc;
    desc.position = {0.0f, 0.1f, 0.0f};
    auto character = world.value().create_character(desc);
    REQUIRE(character);

    float max_height = 0.0f;
    int grounded_ticks = 0;
    for (int i = 0; i < 240; ++i) {
        world.value().set_character_velocity(character.value(), {1.5f, 0.0f, 0.0f});
        REQUIRE(world.value().update(kTick));
        const hue::Vec3 position = world.value().character_position(character.value());
        if (position.y > max_height) {
            max_height = position.y;
        }
        if (world.value().character_grounded(character.value())) {
            ++grounded_ticks;
        }
    }
    CHECK(max_height > 0.5f);       // actually climbed the ramp
    CHECK(grounded_ticks > 200);    // and stayed in ground contact while doing it
}

TEST_CASE("physics: raycast reports the arena floor for the camera probe") {
    auto world = hue::physics::PhysicsWorld::create();
    REQUIRE(world);
    add_ground(world.value());

    const auto hit = world.value().cast_ray({0.0f, 2.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 5.0f);
    CHECK(hit.hit);
    CHECK(hit.fraction == doctest::Approx(0.4f).epsilon(0.01f));
    CHECK(hit.position.y == doctest::Approx(0.0f).epsilon(0.01f));
    CHECK(hit.normal.y == doctest::Approx(1.0f).epsilon(0.01f));

    const auto miss = world.value().cast_ray({0.0f, 2.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 5.0f);
    CHECK_FALSE(miss.hit);
    CHECK(world.value().static_body_count() == 1);
}

TEST_CASE("physics: cooked triangle mesh carries the character and the camera probe") {
    auto world = hue::physics::PhysicsWorld::create();
    REQUIRE(world);

    // Two-triangle 20x20 floor at y = 0, exactly what the game-side cooker
    // emits for the render arena's ground plane.
    const hue::Vec3 positions[4] = {
        {-10.0f, 0.0f, -10.0f},
        {10.0f, 0.0f, -10.0f},
        {10.0f, 0.0f, 10.0f},
        {-10.0f, 0.0f, 10.0f},
    };
    const std::uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    REQUIRE(world.value().add_static_mesh(positions, 4, indices, 6));
    CHECK(world.value().static_body_count() == 1);

    hue::physics::CharacterDesc desc;
    desc.position = {0.0f, 2.0f, 0.0f};
    auto character = world.value().create_character(desc);
    REQUIRE(character);
    for (int i = 0; i < 120; ++i) {
        REQUIRE(world.value().update(1.0f / 60.0f));
    }
    CHECK(world.value().character_grounded(character.value()));
    CHECK(world.value().character_position(character.value()).y ==
          doctest::Approx(0.0f).epsilon(0.05f));

    const auto hit = world.value().cast_ray({1.0f, 3.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, 6.0f);
    CHECK(hit.hit);
    CHECK(hit.fraction == doctest::Approx(0.5f).epsilon(0.01f));

    // Malformed soups are rejected before they reach the cooker.
    CHECK_FALSE(world.value().add_static_mesh(positions, 4, indices, 5)); // not triples
    const std::uint32_t out_of_range[3] = {0, 1, 9};
    CHECK_FALSE(world.value().add_static_mesh(positions, 4, out_of_range, 3));
    CHECK_FALSE(world.value().add_static_mesh(nullptr, 4, indices, 6));
}

TEST_CASE("physics: invalid arguments are rejected") {
    auto world = hue::physics::PhysicsWorld::create();
    REQUIRE(world);
    CHECK_FALSE(world.value().add_static_box({}, hue::Quat{}, {0.0f, 1.0f, 1.0f}));

    hue::physics::CharacterDesc bad;
    bad.radius = -1.0f;
    CHECK_FALSE(world.value().create_character(bad));
    CHECK_FALSE(world.value().update(0.0f));
}
