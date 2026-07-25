// tests/core/bench_math.cpp
//
// Throughput microbench for the math hot paths. No pass/fail thresholds
// (CI machines vary wildly); it prints ns/op so regressions are visible in
// the test log. Sanity CHECKs on the accumulated sinks prevent the
// optimizer from deleting the work.

#include <doctest/doctest.h>

#include "hue/core/math.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>

using hue::Mat4;
using hue::Quat;
using hue::Vec3;
using hue::Vec4;

namespace {

constexpr std::uint32_t kDataCount = 1024;
constexpr std::uint32_t kIterations = 1 << 20; // ~1M ops per measurement

Mat4 g_mats[kDataCount];
Vec4 g_vec4s[kDataCount];
Vec3 g_vec3s[kDataCount];
Quat g_quats[kDataCount];

void fill_data() {
    for (std::uint32_t i = 0; i < kDataCount; ++i) {
        const float f = static_cast<float>(i % 97) * 0.13f + 0.5f;
        g_vec3s[i] = Vec3{f, f * 0.7f + 0.2f, 3.1f - f * 0.01f};
        g_vec4s[i] = Vec4{f, f * 0.5f, 1.0f - f * 0.02f, 1.0f};
        g_quats[i] = Quat::from_axis_angle(g_vec3s[i], f * 0.05f);
        g_mats[i] = Mat4::trs(g_vec3s[i], g_quats[i], {1.0f, 1.0f, 1.0f});
    }
}

template <typename F> double ns_per_op(const char* name, F&& body) {
    using Clock = std::chrono::steady_clock;
    body(); // warm up caches and page in the data
    const auto start = Clock::now();
    body();
    const double ns = std::chrono::duration<double, std::nano>(Clock::now() - start).count();
    const double per_op = ns / static_cast<double>(kIterations);
    std::printf("bench %-22s %8.3f ns/op\n", name, per_op);
    return per_op;
}

} // namespace

TEST_CASE("math: throughput microbench") {
    fill_data();

    float float_sink = 0.0f;
    Mat4 mat_sink;

    ns_per_op("vec4 dot", [&] {
        float acc = 0.0f;
        for (std::uint32_t i = 0; i < kIterations; ++i) {
            acc += dot(g_vec4s[i & (kDataCount - 1)], g_vec4s[(i + 7) & (kDataCount - 1)]);
        }
        float_sink += acc;
    });

    ns_per_op("vec3 normalize", [&] {
        float acc = 0.0f;
        for (std::uint32_t i = 0; i < kIterations; ++i) {
            acc += normalize(g_vec3s[i & (kDataCount - 1)]).x;
        }
        float_sink += acc;
    });

    ns_per_op("quat rotate vec3", [&] {
        float acc = 0.0f;
        for (std::uint32_t i = 0; i < kIterations; ++i) {
            acc += rotate(g_quats[i & (kDataCount - 1)], g_vec3s[(i + 3) & (kDataCount - 1)]).y;
        }
        float_sink += acc;
    });

    ns_per_op("quat slerp", [&] {
        float acc = 0.0f;
        for (std::uint32_t i = 0; i < kIterations; ++i) {
            acc += slerp(g_quats[i & (kDataCount - 1)], g_quats[(i + 5) & (kDataCount - 1)],
                         0.37f)
                       .w;
        }
        float_sink += acc;
    });

    ns_per_op("mat4 * vec4", [&] {
        float acc = 0.0f;
        for (std::uint32_t i = 0; i < kIterations; ++i) {
            acc += (g_mats[i & (kDataCount - 1)] * g_vec4s[(i + 11) & (kDataCount - 1)]).z;
        }
        float_sink += acc;
    });

    ns_per_op("mat4 * mat4", [&] {
        Mat4 acc;
        for (std::uint32_t i = 0; i < kIterations; ++i) {
            acc = g_mats[i & (kDataCount - 1)] * acc;
            if ((i & 255u) == 0u) {
                acc = Mat4::identity(); // keep values from overflowing to inf
            }
        }
        mat_sink = acc;
    });

    ns_per_op("mat4 trs compose", [&] {
        float acc = 0.0f;
        for (std::uint32_t i = 0; i < kIterations; ++i) {
            const std::uint32_t j = i & (kDataCount - 1);
            acc += Mat4::trs(g_vec3s[j], g_quats[j], {1.0f, 2.0f, 1.0f}).m[12];
        }
        float_sink += acc;
    });

    ns_per_op("mat4 general inverse", [&] {
        float acc = 0.0f;
        for (std::uint32_t i = 0; i < kIterations; ++i) {
            acc += g_mats[i & (kDataCount - 1)].inverted().m[14];
        }
        float_sink += acc;
    });

    ns_per_op("mat4 rigid inverse", [&] {
        float acc = 0.0f;
        for (std::uint32_t i = 0; i < kIterations; ++i) {
            acc += g_mats[i & (kDataCount - 1)].inverted_rigid().m[14];
        }
        float_sink += acc;
    });

    ns_per_op("quat nlerp", [&] {
        float acc = 0.0f;
        for (std::uint32_t i = 0; i < kIterations; ++i) {
            acc += nlerp(g_quats[i & (kDataCount - 1)], g_quats[(i + 5) & (kDataCount - 1)],
                         0.37f)
                       .w;
        }
        float_sink += acc;
    });

    CHECK(std::isfinite(float_sink));
    CHECK(std::isfinite(mat_sink.m[0]));
}
