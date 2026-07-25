// tests/core/test_spring.cpp

#include <doctest/doctest.h>

#include "hue/core/spring.h"

using hue::Vec3;

TEST_CASE("spring: exponential damper converges and is framerate independent") {
    const float omega = hue::halflife_to_omega(0.1f);

    // Converges toward the target.
    float value = 0.0f;
    for (int i = 0; i < 120; ++i) {
        value = hue::damp(value, 10.0f, omega, 1.0f / 60.0f);
    }
    CHECK(value == doctest::Approx(10.0f).epsilon(1e-3f));

    // One big step equals two half steps (exact property of the closed form).
    const float one_step = hue::damp(0.0f, 10.0f, omega, 0.2f);
    float two_steps = hue::damp(0.0f, 10.0f, omega, 0.1f);
    two_steps = hue::damp(two_steps, 10.0f, omega, 0.1f);
    CHECK(one_step == doctest::Approx(two_steps).epsilon(1e-5f));
}

TEST_CASE("spring: critically damped spring converges without overshoot") {
    const float omega = hue::halflife_to_omega(0.05f);
    float position = 0.0f;
    float velocity = 0.0f;

    float max_position = 0.0f;
    for (int i = 0; i < 600; ++i) {
        hue::spring_damp(position, velocity, 1.0f, omega, 1.0f / 60.0f);
        max_position = position > max_position ? position : max_position;
    }

    CHECK(position == doctest::Approx(1.0f).epsilon(1e-3f));
    CHECK(velocity == doctest::Approx(0.0f).epsilon(1e-3f));
    // Critically damped from rest never crosses the target.
    CHECK(max_position <= 1.0f + 1e-4f);
}

TEST_CASE("spring: initial velocity decays back to the target") {
    const float omega = hue::halflife_to_omega(0.05f);
    float position = 1.0f; // already at target
    float velocity = 50.0f; // but moving away fast (a hit reaction kick)

    for (int i = 0; i < 600; ++i) {
        hue::spring_damp(position, velocity, 1.0f, omega, 1.0f / 60.0f);
    }
    CHECK(position == doctest::Approx(1.0f).epsilon(1e-3f));
    CHECK(velocity == doctest::Approx(0.0f).epsilon(1e-3f));
}

TEST_CASE("spring: vec3 overload tracks a moving camera target") {
    const float omega = hue::halflife_to_omega(0.1f);
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 velocity{};
    const Vec3 target{3.0f, -4.0f, 12.0f};

    for (int i = 0; i < 600; ++i) {
        hue::spring_damp(position, velocity, target, omega, 1.0f / 60.0f);
    }
    CHECK(position.x == doctest::Approx(3.0f).epsilon(1e-3f));
    CHECK(position.y == doctest::Approx(-4.0f).epsilon(1e-3f));
    CHECK(position.z == doctest::Approx(12.0f).epsilon(1e-3f));
}
