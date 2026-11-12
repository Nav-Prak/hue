// tests/render/test_shadow.cpp
#include "hue/render/renderer.h"

#include <cmath>
#include <doctest/doctest.h>
#include <limits>

namespace {
hue::Vec3 project(const hue::Mat4& matrix, hue::Vec3 point) {
    const auto clip = matrix * hue::Vec4{point.x, point.y, point.z, 1.0f};
    REQUIRE(clip.w == doctest::Approx(1.0f)); // orthographic: no perspective divide
    return {clip.x, clip.y, clip.z};
}
bool finite(const hue::Mat4& matrix) {
    for (const float value : matrix.m)
        if (!std::isfinite(value))
            return false;
    return true;
}
} // namespace

TEST_CASE("math: orthographic maps its box to Vulkan clip space") {
    const auto ortho = hue::Mat4::orthographic(-2.0f, 6.0f, -1.0f, 3.0f, 0.5f, 10.5f);
    const auto near_corner = project(ortho, {-2.0f, -1.0f, -0.5f});
    CHECK(near_corner.x == doctest::Approx(-1.0f));
    CHECK(near_corner.y == doctest::Approx(-1.0f));
    CHECK(near_corner.z == doctest::Approx(0.0f));
    const auto far_corner = project(ortho, {6.0f, 3.0f, -10.5f});
    CHECK(far_corner.x == doctest::Approx(1.0f));
    CHECK(far_corner.y == doctest::Approx(1.0f));
    CHECK(far_corner.z == doctest::Approx(1.0f));
    const auto middle = project(ortho, {2.0f, 1.0f, -5.5f});
    CHECK(middle.x == doctest::Approx(0.0f));
    CHECK(middle.y == doctest::Approx(0.0f));
    CHECK(middle.z == doctest::Approx(0.5f));
}

TEST_CASE("shadow: the light box contains its whole sphere for any sun direction") {
    const hue::Vec3 center{1.0f, 0.5f, -2.0f};
    const float radius = 16.0f;
    for (const hue::Vec3 direction :
         {hue::Vec3{0.4f, -1.0f, 0.3f}, hue::Vec3{-0.5f, -1.0f, 0.35f}, hue::Vec3{0.0f, -1.0f, 0.0f},
          hue::Vec3{3.0f, -0.8f, 0.0f}, hue::Vec3{0.0f, -0.3f, -1.0f}}) {
        const auto matrix = hue::directional_shadow_matrix(direction, center, radius);
        REQUIRE(finite(matrix));
        const auto middle = project(matrix, center);
        CHECK(middle.x == doctest::Approx(0.0f).epsilon(0.001));
        CHECK(middle.y == doctest::Approx(0.0f).epsilon(0.001));
        // Sample the sphere surface; everything must land inside the map.
        for (int i = 0; i < 64; ++i) {
            const float a = static_cast<float>(i) * 0.7f;
            const float b = static_cast<float>(i) * 1.3f;
            const hue::Vec3 offset{std::cos(a) * std::cos(b), std::sin(b), std::sin(a) * std::cos(b)};
            const auto p = project(matrix, center + offset * radius);
            CHECK(std::abs(p.x) <= 1.001f);
            CHECK(std::abs(p.y) <= 1.001f);
            CHECK(p.z > 0.0f);
            CHECK(p.z < 1.0f);
        }
        // Depth increases along the direction the light travels.
        const hue::Vec3 along = hue::normalize(direction);
        CHECK(project(matrix, center + along * 4.0f).z > project(matrix, center - along * 4.0f).z);
    }
}

TEST_CASE("shadow: degenerate light parameters fall back instead of producing NaNs") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const hue::Vec3 direction :
         {hue::Vec3{0.0f, 0.0f, 0.0f}, hue::Vec3{nan, -1.0f, 0.0f}, hue::Vec3{0.0f, inf, 0.0f},
          hue::Vec3{0.0f, 1.0f, 0.0f}}) {
        for (const float radius : {16.0f, 0.0f, -5.0f, nan, inf, 1.0e9f}) {
            for (const hue::Vec3 center : {hue::Vec3{}, hue::Vec3{nan, 0.0f, 0.0f}}) {
                const auto matrix = hue::directional_shadow_matrix(direction, center, radius);
                CHECK(finite(matrix));
            }
        }
    }
}
