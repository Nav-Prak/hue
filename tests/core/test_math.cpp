// tests/core/test_math.cpp
//
// Reference-value tests (Week 3 DoD): expected results are hand-computed,
// not round-tripped through the library itself.

#include <doctest/doctest.h>

#include "hue/core/math.h"

using hue::Mat4;
using hue::Quat;
using hue::Vec3;
using hue::Vec4;

namespace {

constexpr float kTol = 1e-4f;

void check_vec3(Vec3 actual, float x, float y, float z) {
    CHECK(actual.x == doctest::Approx(x).epsilon(kTol));
    CHECK(actual.y == doctest::Approx(y).epsilon(kTol));
    CHECK(actual.z == doctest::Approx(z).epsilon(kTol));
}

} // namespace

TEST_CASE("math: scalar helpers") {
    CHECK(hue::radians(180.0f) == doctest::Approx(hue::kPi));
    CHECK(hue::degrees(hue::kPi) == doctest::Approx(180.0f));
    CHECK(hue::lerp(2.0f, 6.0f, 0.25f) == doctest::Approx(3.0f));
    CHECK(hue::clamp(5.0f, 0.0f, 1.0f) == 1.0f);
    CHECK(hue::saturate(-0.5f) == 0.0f);
}

TEST_CASE("math: vec3 dot cross length normalize") {
    const Vec3 a{1.0f, 2.0f, 3.0f};
    const Vec3 b{4.0f, -5.0f, 6.0f};

    CHECK(dot(a, b) == doctest::Approx(12.0f)); // 4 - 10 + 18

    // (2*6 - 3*(-5), 3*4 - 1*6, 1*(-5) - 2*4) = (27, 6, -13)
    check_vec3(cross(a, b), 27.0f, 6.0f, -13.0f);

    CHECK(length(Vec3{3.0f, 4.0f, 0.0f}) == doctest::Approx(5.0f));
    check_vec3(normalize(Vec3{0.0f, 0.0f, 9.0f}), 0.0f, 0.0f, 1.0f);
    check_vec3(normalize(Vec3{}), 0.0f, 0.0f, 0.0f); // zero-safe, no NaN
}

TEST_CASE("math: vec4 simd add and dot") {
    const Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    const Vec4 b{5.0f, 6.0f, 7.0f, 8.0f};
    const Vec4 sum = a + b;
    CHECK(sum.x == 6.0f);
    CHECK(sum.y == 8.0f);
    CHECK(sum.z == 10.0f);
    CHECK(sum.w == 12.0f);
    CHECK(dot(a, b) == doctest::Approx(70.0f)); // 5 + 12 + 21 + 32
    const Vec4 scaled = a * 2.0f;
    CHECK(scaled.w == 8.0f);
}

TEST_CASE("math: quaternion axis-angle rotation") {
    // 90 degrees about +Z sends +X to +Y.
    const Quat q = Quat::from_axis_angle({0.0f, 0.0f, 1.0f}, hue::radians(90.0f));
    check_vec3(rotate(q, {1.0f, 0.0f, 0.0f}), 0.0f, 1.0f, 0.0f);
    check_vec3(rotate(q, {0.0f, 1.0f, 0.0f}), -1.0f, 0.0f, 0.0f);

    // Composition: two 90-degree rotations about Z = 180 degrees.
    const Quat twice = q * q;
    check_vec3(rotate(twice, {1.0f, 0.0f, 0.0f}), -1.0f, 0.0f, 0.0f);
}

TEST_CASE("math: quaternion slerp endpoints and midpoint") {
    const Quat identity{};
    const Quat quarter = Quat::from_axis_angle({0.0f, 0.0f, 1.0f}, hue::radians(90.0f));

    const Quat at_start = slerp(identity, quarter, 0.0f);
    check_vec3(rotate(at_start, {1.0f, 0.0f, 0.0f}), 1.0f, 0.0f, 0.0f);

    const Quat at_end = slerp(identity, quarter, 1.0f);
    check_vec3(rotate(at_end, {1.0f, 0.0f, 0.0f}), 0.0f, 1.0f, 0.0f);

    // Midpoint is a 45-degree rotation: +X lands at (cos45, sin45, 0).
    const Quat mid = slerp(identity, quarter, 0.5f);
    check_vec3(rotate(mid, {1.0f, 0.0f, 0.0f}), 0.70710678f, 0.70710678f, 0.0f);
}

TEST_CASE("math: mat4 translation scaling rotation") {
    const Mat4 t = Mat4::translation({1.0f, 2.0f, 3.0f});
    check_vec3(t.transform_point({0.0f, 0.0f, 0.0f}), 1.0f, 2.0f, 3.0f);
    // Directions ignore translation.
    check_vec3(t.transform_vector({0.0f, 0.0f, 1.0f}), 0.0f, 0.0f, 1.0f);

    const Mat4 s = Mat4::scaling({2.0f, 3.0f, 4.0f});
    check_vec3(s.transform_point({1.0f, 1.0f, 1.0f}), 2.0f, 3.0f, 4.0f);

    const Quat q = Quat::from_axis_angle({0.0f, 0.0f, 1.0f}, hue::radians(90.0f));
    const Mat4 r = Mat4::rotation(q);
    check_vec3(r.transform_point({1.0f, 0.0f, 0.0f}), 0.0f, 1.0f, 0.0f);
}

TEST_CASE("math: mat4 trs applies scale then rotation then translation") {
    const Quat rot_z_90 = Quat::from_axis_angle({0.0f, 0.0f, 1.0f}, hue::radians(90.0f));
    const Mat4 m = Mat4::trs({0.0f, 0.0f, 5.0f}, rot_z_90, {2.0f, 2.0f, 2.0f});
    // (1,0,0) -> scale -> (2,0,0) -> rotate -> (0,2,0) -> translate -> (0,2,5)
    check_vec3(m.transform_point({1.0f, 0.0f, 0.0f}), 0.0f, 2.0f, 5.0f);
}

TEST_CASE("math: mat4 multiply against hand-computed product") {
    // Rotate 90 about Z, then translate: T * R applied to a column vector.
    const Quat rot_z_90 = Quat::from_axis_angle({0.0f, 0.0f, 1.0f}, hue::radians(90.0f));
    const Mat4 product = Mat4::translation({10.0f, 0.0f, 0.0f}) * Mat4::rotation(rot_z_90);
    check_vec3(product.transform_point({1.0f, 0.0f, 0.0f}), 10.0f, 1.0f, 0.0f);

    // Identity is neutral on both sides.
    const Mat4 vs_identity = product * Mat4::identity();
    for (int i = 0; i < 16; ++i) {
        CHECK(vs_identity.m[i] == doctest::Approx(product.m[i]).epsilon(kTol));
    }
}

TEST_CASE("math: mat4 perspective reference elements") {
    // fovy 90, aspect 1, near 0.1, far 100 (Vulkan 0..1 depth).
    const Mat4 p = Mat4::perspective(hue::radians(90.0f), 1.0f, 0.1f, 100.0f);
    CHECK(p.at(0, 0) == doctest::Approx(1.0f).epsilon(kTol));
    CHECK(p.at(1, 1) == doctest::Approx(1.0f).epsilon(kTol));
    CHECK(p.at(2, 2) == doctest::Approx(-1.001001f).epsilon(kTol));
    CHECK(p.at(3, 2) == doctest::Approx(-1.0f).epsilon(kTol));
    CHECK(p.at(2, 3) == doctest::Approx(-0.1001001f).epsilon(kTol));
    CHECK(p.at(3, 3) == doctest::Approx(0.0f));

    // Depth check: near plane maps to 0, far plane maps to 1 after divide.
    const Vec4 near_clip = p * Vec4{0.0f, 0.0f, -0.1f, 1.0f};
    CHECK(near_clip.z / near_clip.w == doctest::Approx(0.0f).epsilon(kTol));
    const Vec4 far_clip = p * Vec4{0.0f, 0.0f, -100.0f, 1.0f};
    CHECK(far_clip.z / far_clip.w == doctest::Approx(1.0f).epsilon(kTol));
}

TEST_CASE("math: mat4 look_at maps eye to origin and target onto -Z") {
    const Mat4 view = Mat4::look_at({0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    check_vec3(view.transform_point({0.0f, 0.0f, 5.0f}), 0.0f, 0.0f, 0.0f);
    check_vec3(view.transform_point({0.0f, 0.0f, 0.0f}), 0.0f, 0.0f, -5.0f);
    // Right-handed: world +X stays +X for this camera.
    check_vec3(view.transform_vector({1.0f, 0.0f, 0.0f}), 1.0f, 0.0f, 0.0f);
}

TEST_CASE("math: quaternion nlerp matches slerp for small arcs") {
    const Quat a = Quat::from_axis_angle({0.0f, 1.0f, 0.0f}, hue::radians(10.0f));
    const Quat b = Quat::from_axis_angle({0.0f, 1.0f, 0.0f}, hue::radians(25.0f));
    const Quat n = nlerp(a, b, 0.4f);
    const Quat s = slerp(a, b, 0.4f);
    const Vec3 vn = rotate(n, {1.0f, 0.0f, 0.0f});
    const Vec3 vs = rotate(s, {1.0f, 0.0f, 0.0f});
    CHECK(vn.x == doctest::Approx(vs.x).epsilon(1e-3f));
    CHECK(vn.y == doctest::Approx(vs.y).epsilon(1e-3f));
    CHECK(vn.z == doctest::Approx(vs.z).epsilon(1e-3f));

    // Shortest-path handling: interpolating toward -b (same rotation) must
    // not swing the long way around.
    const Quat negated_b{-b.x, -b.y, -b.z, -b.w};
    const Quat via_negated = nlerp(a, negated_b, 1.0f);
    const Vec3 direct = rotate(b, {1.0f, 0.0f, 0.0f});
    const Vec3 flipped = rotate(via_negated, {1.0f, 0.0f, 0.0f});
    CHECK(flipped.x == doctest::Approx(direct.x).epsilon(1e-4f));
    CHECK(flipped.z == doctest::Approx(direct.z).epsilon(1e-4f));
}

TEST_CASE("math: mat4 rigid inverse matches general inverse") {
    const Quat q = Quat::from_axis_angle({0.2f, 0.9f, -0.4f}, hue::radians(52.0f));
    const Mat4 rigid = Mat4::trs({7.0f, -3.0f, 2.5f}, q, {1.0f, 1.0f, 1.0f});

    const Mat4 fast = rigid.inverted_rigid();
    const Mat4 general = rigid.inverted();
    for (int i = 0; i < 16; ++i) {
        CHECK(fast.m[i] == doctest::Approx(general.m[i]).epsilon(1e-3f));
    }

    const Mat4 should_be_identity = rigid * fast;
    const Mat4 identity = Mat4::identity();
    for (int i = 0; i < 16; ++i) {
        CHECK(should_be_identity.m[i] == doctest::Approx(identity.m[i]).epsilon(1e-3f));
    }
}

TEST_CASE("math: mat4 inverse recovers identity") {
    const Quat q = Quat::from_axis_angle({0.3f, 0.7f, 0.2f}, hue::radians(37.0f));
    const Mat4 m = Mat4::trs({4.0f, -2.0f, 9.0f}, q, {2.0f, 0.5f, 3.0f});
    const Mat4 should_be_identity = m * m.inverted();
    const Mat4 identity = Mat4::identity();
    for (int i = 0; i < 16; ++i) {
        CHECK(should_be_identity.m[i] == doctest::Approx(identity.m[i]).epsilon(1e-3f));
    }
}

TEST_CASE("math: mat4 transpose swaps rows and columns") {
    Mat4 m;
    m.set(0, 1, 42.0f);
    m.set(3, 0, -7.0f);
    const Mat4 t = m.transposed();
    CHECK(t.at(1, 0) == 42.0f);
    CHECK(t.at(0, 3) == -7.0f);
}
