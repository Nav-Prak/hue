// tests/game/test_follow_camera.cpp
#include "follow_camera.h"
#include <cmath>
#include <doctest/doctest.h>

namespace {
constexpr float kDt = 1.0f / 60.0f;
hue::Vec2 project(const hue::Camera& camera, hue::Vec3 point) {
    const auto clip = camera.view_projection() * hue::Vec4{point.x, point.y, point.z, 1.0f};
    REQUIRE(clip.w > 0.0f);
    return {clip.x / clip.w, clip.y / clip.w};
}
} // namespace

TEST_CASE("follow camera: opening and lock-on separate the fighters at common aspects") {
    auto physics = hue::physics::PhysicsWorld::create();
    REQUIRE(physics);
    hue::Input input;
    const hue::Vec3 feet{-0.8f, 0.05f, 3.0f};
    const hue::Vec3 opponent{0.8f, 1.25f, 3.0f};
    for (bool locked : {false, true}) {
        FollowCamera camera;
        for (int frame = 0; frame < 120; ++frame)
            camera.update(input, kDt, feet, physics.value(), locked ? &opponent : nullptr);
        for (float aspect : {4.0f / 3.0f, 16.0f / 9.0f, 21.0f / 9.0f}) {
            const auto view = camera.camera(aspect);
            const auto player = project(view, feet + hue::Vec3{0.0f, 1.2f, 0.0f});
            const auto enemy = project(view, opponent);
            CHECK(enemy.x - player.x > 0.12f);
            CHECK(std::abs(player.x) < 0.8f);
            CHECK(std::abs(enemy.x) < 0.8f);
            CHECK(std::abs(player.y) < 0.8f);
            CHECK(std::abs(enemy.y) < 0.8f);
        }
    }
}

TEST_CASE("follow camera: new boom obstruction clamps immediately then recovers slowly") {
    auto clear = hue::physics::PhysicsWorld::create();
    REQUIRE(clear);
    auto blocked = hue::physics::PhysicsWorld::create();
    REQUIRE(blocked);
    hue::Input input;
    FollowCamera camera;
    const hue::Vec3 feet{};
    camera.update(input, kDt, feet, clear.value());
    const auto open_position = camera.position();
    // Put a box halfway along the observed boom, not at a guessed wall location.
    const hue::Vec3 right{std::cos(camera.yaw()), 0.0f, -std::sin(camera.yaw())};
    const auto focus = feet + hue::Vec3{0.0f, 1.5f, 0.0f} + right * 0.85f;
    REQUIRE(blocked.value().add_static_box((focus + open_position) * 0.5f, {}, {0.3f, 0.3f, 0.3f}));
    const auto direction = hue::normalize(open_position - focus);
    const auto hit = blocked.value().cast_ray(focus, direction, 5.05f);
    REQUIRE(hit.hit);
    camera.update(input, kDt, feet, blocked.value());
    const float blocked_length = hue::length(camera.position() - focus);
    CHECK(blocked_length <= hit.fraction * 5.05f - 0.24f);
    camera.update(input, kDt, feet, clear.value());
    CHECK(hue::length(camera.position() - focus) > blocked_length);
    CHECK(hue::length(camera.position() - focus) < 4.8f);
}

TEST_CASE("follow camera: shoulder probe keeps the eye on the safe side of a wall") {
    auto physics = hue::physics::PhysicsWorld::create();
    REQUIRE(physics);
    FollowCamera camera;
    hue::Input input;
    const hue::Vec3 right{std::cos(camera.yaw()), 0.0f, -std::sin(camera.yaw())};
    const auto rotation = hue::Quat::from_axis_angle({0.0f, 1.0f, 0.0f}, camera.yaw());
    REQUIRE(physics.value().add_static_box(right * 0.7f + hue::Vec3{0.0f, 2.0f, 0.0f}, rotation,
                                           {0.1f, 5.0f, 10.0f}));
    camera.update(input, kDt, {}, physics.value());
    CHECK(hue::dot(camera.position(), right) < 0.4f);
    CHECK(hue::dot(camera.position(), right) >= 0.0f);
}
