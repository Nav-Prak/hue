// tests/core/test_geometry.cpp

#include <doctest/doctest.h>

#include "hue/core/geometry.h"

using hue::Aabb;
using hue::Frustum;
using hue::Mat4;
using hue::Quat;
using hue::Vec3;

TEST_CASE("geometry: aabb center extents contains intersects") {
    const Aabb box{{-1.0f, -2.0f, -3.0f}, {3.0f, 2.0f, 1.0f}};
    CHECK(box.center().x == doctest::Approx(1.0f));
    CHECK(box.half_extents().y == doctest::Approx(2.0f));

    CHECK(box.contains({0.0f, 0.0f, 0.0f}));
    CHECK(box.contains({3.0f, 2.0f, 1.0f})); // boundary is inside
    CHECK_FALSE(box.contains({3.1f, 0.0f, 0.0f}));

    const Aabb touching{{3.0f, 0.0f, 0.0f}, {5.0f, 1.0f, 1.0f}};
    CHECK(box.intersects(touching));
    const Aabb separate{{10.0f, 10.0f, 10.0f}, {11.0f, 11.0f, 11.0f}};
    CHECK_FALSE(box.intersects(separate));

    const Aabb merged = box.merged(separate);
    CHECK(merged.min_point.x == -1.0f);
    CHECK(merged.max_point.z == 11.0f);

    const Aabb grown = box.expanded_to_include({-5.0f, 0.0f, 0.0f});
    CHECK(grown.min_point.x == -5.0f);
}

TEST_CASE("geometry: aabb transform stays tight under rotation") {
    // Box centered at (2,0,0), extents (1, 0.5, 0.25), rotated 90 about Z:
    // center moves to (0,2,0), x/y extents swap.
    const Aabb box = Aabb::from_center_extents({2.0f, 0.0f, 0.0f}, {1.0f, 0.5f, 0.25f});
    const Quat rot_z_90 = Quat::from_axis_angle({0.0f, 0.0f, 1.0f}, hue::radians(90.0f));
    const Aabb rotated = box.transformed(Mat4::rotation(rot_z_90));

    CHECK(rotated.center().x == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(rotated.center().y == doctest::Approx(2.0f).epsilon(1e-4f));
    CHECK(rotated.half_extents().x == doctest::Approx(0.5f).epsilon(1e-4f));
    CHECK(rotated.half_extents().y == doctest::Approx(1.0f).epsilon(1e-4f));
    CHECK(rotated.half_extents().z == doctest::Approx(0.25f).epsilon(1e-4f));
}

TEST_CASE("geometry: frustum culls boxes against a perspective camera") {
    // Camera at origin looking down -Z, 90 degree fov, square aspect:
    // at depth z = -10 the visible square is x,y in -10..10.
    const Mat4 proj = Mat4::perspective(hue::radians(90.0f), 1.0f, 0.1f, 100.0f);
    const Mat4 view =
        Mat4::look_at({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
    const Frustum frustum = Frustum::from_view_projection(proj * view);

    CHECK(frustum.contains_point({0.0f, 0.0f, -1.0f}));
    CHECK_FALSE(frustum.contains_point({0.0f, 0.0f, 1.0f})); // behind camera

    const auto unit_box_at = [](Vec3 center) {
        return Aabb::from_center_extents(center, {0.5f, 0.5f, 0.5f});
    };

    CHECK(frustum.intersects(unit_box_at({0.0f, 0.0f, -10.0f})));   // dead ahead
    CHECK(frustum.intersects(unit_box_at({9.8f, 0.0f, -10.0f})));   // straddles right plane
    CHECK_FALSE(frustum.intersects(unit_box_at({0.0f, 0.0f, 10.0f})));   // behind camera
    CHECK_FALSE(frustum.intersects(unit_box_at({30.0f, 0.0f, -10.0f}))); // right of frustum
    CHECK_FALSE(frustum.intersects(unit_box_at({0.0f, -30.0f, -10.0f}))); // below frustum
    CHECK_FALSE(frustum.intersects(unit_box_at({0.0f, 0.0f, -200.0f})));  // beyond far plane

    // Closer than the near plane (0.1): a tiny box at z = -0.02.
    const Aabb near_box = Aabb::from_center_extents({0.0f, 0.0f, -0.02f}, {0.01f, 0.01f, 0.01f});
    CHECK_FALSE(frustum.intersects(near_box));

    // Huge box surrounding the whole frustum still intersects.
    const Aabb huge = Aabb::from_center_extents({0.0f, 0.0f, -50.0f}, {500.0f, 500.0f, 500.0f});
    CHECK(frustum.intersects(huge));
}
