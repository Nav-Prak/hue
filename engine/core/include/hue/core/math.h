// engine/core/include/hue/core/math.h
//
// From-scratch math library (Week 3 spec): scalar helpers, Vec2/Vec3/Vec4,
// Quat, and column-major Mat4. Vec4/Mat4 hot paths use SSE on x64 with a
// scalar fallback elsewhere. Conventions:
//   - column-major storage, column vectors: transformed = M * v
//   - right-handed world, camera looks down -Z in view space
//   - clip space depth 0..1 (Vulkan); renderer handles Y flip
// No glm on purpose (AI Directive 3): the engine owns its conventions.

#pragma once

#include <cmath>
#include <cstddef>

#if defined(_M_X64) || defined(__SSE4_2__)
#define HUE_SIMD_SSE 1
#include <smmintrin.h> // SSE4.1/4.2
#else
#define HUE_SIMD_SSE 0
#endif

namespace hue {

// ---------------------------------------------------------------- scalars

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 2.0f * kPi;
inline constexpr float kHalfPi = 0.5f * kPi;
inline constexpr float kEpsilon = 1e-6f;

[[nodiscard]] constexpr float radians(float degrees) noexcept {
    return degrees * (kPi / 180.0f);
}
[[nodiscard]] constexpr float degrees(float rad) noexcept {
    return rad * (180.0f / kPi);
}
[[nodiscard]] constexpr float lerp(float a, float b, float t) noexcept {
    return a + (b - a) * t;
}
[[nodiscard]] constexpr float clamp(float value, float low, float high) noexcept {
    return value < low ? low : (value > high ? high : value);
}
[[nodiscard]] constexpr float saturate(float value) noexcept {
    return clamp(value, 0.0f, 1.0f);
}
[[nodiscard]] inline bool nearly_equal(float a, float b, float tolerance = 1e-5f) noexcept {
    return std::fabs(a - b) <= tolerance;
}

// Fast reciprocal square root: rsqrtss (~22-bit) plus one Newton-Raphson
// step brings relative error to ~2e-7, at roughly half the latency of
// sqrtss + divss. Normalization goes through this instead of dividing.
[[nodiscard]] inline float inv_sqrt(float x) noexcept {
#if HUE_SIMD_SSE
    const float estimate = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x)));
    return estimate * (1.5f - 0.5f * x * estimate * estimate);
#else
    return 1.0f / std::sqrt(x);
#endif
}

// ---------------------------------------------------------------- Vec2/Vec3

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    friend constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept {
        return {a.x + b.x, a.y + b.y, a.z + b.z};
    }
    friend constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }
    friend constexpr Vec3 operator-(Vec3 a) noexcept { return {-a.x, -a.y, -a.z}; }
    friend constexpr Vec3 operator*(Vec3 a, float s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
    friend constexpr Vec3 operator*(float s, Vec3 a) noexcept { return a * s; }
    friend constexpr Vec3 operator*(Vec3 a, Vec3 b) noexcept { // component-wise
        return {a.x * b.x, a.y * b.y, a.z * b.z};
    }
    // One divide + three multiplies instead of three divides. The single
    // rounding difference is far below kEpsilon and divss is ~4x the cost
    // of mulss with no pipelining between the three lanes.
    friend constexpr Vec3 operator/(Vec3 a, float s) noexcept {
        const float inv = 1.0f / s;
        return {a.x * inv, a.y * inv, a.z * inv};
    }
};

[[nodiscard]] constexpr float dot(Vec3 a, Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
[[nodiscard]] constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] constexpr float length_squared(Vec3 v) noexcept {
    return dot(v, v);
}
[[nodiscard]] inline float length(Vec3 v) noexcept {
    return std::sqrt(length_squared(v));
}
// Zero-length input returns the zero vector (no exceptions, no NaN).
// One rsqrt + three multiplies; the old sqrt + divide chain serialized.
[[nodiscard]] inline Vec3 normalize(Vec3 v) noexcept {
    const float len_sq = length_squared(v);
    if (len_sq <= kEpsilon * kEpsilon) {
        return {};
    }
    return v * inv_sqrt(len_sq);
}
[[nodiscard]] constexpr Vec3 lerp(Vec3 a, Vec3 b, float t) noexcept {
    return a + (b - a) * t;
}
[[nodiscard]] constexpr Vec3 min(Vec3 a, Vec3 b) noexcept {
    return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z};
}
[[nodiscard]] constexpr Vec3 max(Vec3 a, Vec3 b) noexcept {
    return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z};
}
[[nodiscard]] inline bool nearly_equal(Vec3 a, Vec3 b, float tolerance = 1e-5f) noexcept {
    return nearly_equal(a.x, b.x, tolerance) && nearly_equal(a.y, b.y, tolerance) &&
           nearly_equal(a.z, b.z, tolerance);
}

// ---------------------------------------------------------------- Vec4

// 16-byte aligned so SSE ops use aligned load/store (loadu on a split cache
// line costs an extra cycle or more per access; aligned never splits).
struct alignas(16) Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    friend Vec4 operator+(Vec4 a, Vec4 b) noexcept {
#if HUE_SIMD_SSE
        Vec4 out;
        _mm_store_ps(&out.x, _mm_add_ps(_mm_load_ps(&a.x), _mm_load_ps(&b.x)));
        return out;
#else
        return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
#endif
    }
    friend Vec4 operator-(Vec4 a, Vec4 b) noexcept {
#if HUE_SIMD_SSE
        Vec4 out;
        _mm_store_ps(&out.x, _mm_sub_ps(_mm_load_ps(&a.x), _mm_load_ps(&b.x)));
        return out;
#else
        return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
#endif
    }
    friend Vec4 operator*(Vec4 a, float s) noexcept {
#if HUE_SIMD_SSE
        Vec4 out;
        _mm_store_ps(&out.x, _mm_mul_ps(_mm_load_ps(&a.x), _mm_set1_ps(s)));
        return out;
#else
        return {a.x * s, a.y * s, a.z * s, a.w * s};
#endif
    }
    friend Vec4 operator*(float s, Vec4 a) noexcept { return a * s; }
};

// mul + movshdup/movehl horizontal reduction instead of dpps: the dpps
// microcode is high-latency on Intel and cracks into 3-4 uops on AMD,
// while this sequence is three cheap single-uop instructions.
[[nodiscard]] inline float dot(Vec4 a, Vec4 b) noexcept {
#if HUE_SIMD_SSE
    const __m128 products = _mm_mul_ps(_mm_load_ps(&a.x), _mm_load_ps(&b.x));
    __m128 shuffled = _mm_movehdup_ps(products);        // y y w w
    __m128 sums = _mm_add_ps(products, shuffled);       // x+y . z+w .
    shuffled = _mm_movehl_ps(shuffled, sums);           // z+w . . .
    return _mm_cvtss_f32(_mm_add_ss(sums, shuffled));   // x+y+z+w
#else
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
#endif
}

// ---------------------------------------------------------------- Quat

// Rotation quaternion (x, y, z, w), w is the scalar part. Matches glTF layout.
// Aligned like Vec4 so bone palettes can be processed with aligned SSE.
struct alignas(16) Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    [[nodiscard]] static Quat from_axis_angle(Vec3 axis, float angle_radians) noexcept {
        const Vec3 n = normalize(axis);
        const float half = 0.5f * angle_radians;
        const float s = std::sin(half);
        return {n.x * s, n.y * s, n.z * s, std::cos(half)};
    }

    // Hamilton product: (a * b) rotates by b first, then a.
    friend constexpr Quat operator*(Quat a, Quat b) noexcept {
        return {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        };
    }
};

[[nodiscard]] constexpr float dot(Quat a, Quat b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
[[nodiscard]] constexpr Quat conjugate(Quat q) noexcept {
    return {-q.x, -q.y, -q.z, q.w};
}
[[nodiscard]] inline Quat normalize(Quat q) noexcept {
    const float len_sq = dot(q, q);
    if (len_sq <= kEpsilon * kEpsilon) {
        return {};
    }
    const float inv = inv_sqrt(len_sq);
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

// Rotate a vector: v' = v + 2w(u x v) + 2(u x (u x v)) with u = (x,y,z).
[[nodiscard]] inline Vec3 rotate(Quat q, Vec3 v) noexcept {
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 uv = cross(u, v);
    const Vec3 uuv = cross(u, uv);
    return v + (uv * q.w + uuv) * 2.0f;
}

// Shortest-path spherical interpolation; falls back to nlerp when the arc is
// tiny (slerp's sin() denominator degenerates).
[[nodiscard]] inline Quat slerp(Quat a, Quat b, float t) noexcept {
    float cos_theta = dot(a, b);
    if (cos_theta < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        cos_theta = -cos_theta;
    }
    float wa = 1.0f - t;
    float wb = t;
    if (cos_theta < 0.9995f) {
        const float theta = std::acos(cos_theta);
        const float inv_sin = 1.0f / std::sin(theta);
        wa = std::sin(wa * theta) * inv_sin;
        wb = std::sin(wb * theta) * inv_sin;
    }
    return normalize(Quat{
        wa * a.x + wb * b.x,
        wa * a.y + wb * b.y,
        wa * a.z + wb * b.z,
        wa * a.w + wb * b.w,
    });
}

// Normalized lerp: no acos/sin, so ~3x cheaper than slerp. Not constant
// angular velocity, but for the small arcs typical of per-frame animation
// blending the difference is invisible. Use slerp for large-arc cases
// (cinematic cameras, big snap corrections).
[[nodiscard]] inline Quat nlerp(Quat a, Quat b, float t) noexcept {
    const float sign = dot(a, b) < 0.0f ? -1.0f : 1.0f;
    return normalize(Quat{
        a.x + (sign * b.x - a.x) * t,
        a.y + (sign * b.y - a.y) * t,
        a.z + (sign * b.z - a.z) * t,
        a.w + (sign * b.w - a.w) * t,
    });
}

// ---------------------------------------------------------------- Mat4

// Column-major 4x4: m[column * 4 + row], matching glTF and GLSL std140.
// 16-byte aligned so each column is an aligned SSE lane.
struct alignas(16) Mat4 {
    float m[16] = {
        1.0f, 0.0f, 0.0f, 0.0f, //
        0.0f, 1.0f, 0.0f, 0.0f, //
        0.0f, 0.0f, 1.0f, 0.0f, //
        0.0f, 0.0f, 0.0f, 1.0f, //
    };

    [[nodiscard]] constexpr float at(std::size_t row, std::size_t column) const noexcept {
        return m[column * 4 + row];
    }
    constexpr void set(std::size_t row, std::size_t column, float value) noexcept {
        m[column * 4 + row] = value;
    }

    [[nodiscard]] static constexpr Mat4 identity() noexcept { return {}; }

    [[nodiscard]] static constexpr Mat4 translation(Vec3 t) noexcept {
        Mat4 out;
        out.m[12] = t.x;
        out.m[13] = t.y;
        out.m[14] = t.z;
        return out;
    }

    [[nodiscard]] static constexpr Mat4 scaling(Vec3 s) noexcept {
        Mat4 out;
        out.m[0] = s.x;
        out.m[5] = s.y;
        out.m[10] = s.z;
        return out;
    }

    [[nodiscard]] static constexpr Mat4 rotation(Quat q) noexcept {
        const float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
        const float xx = q.x * x2, xy = q.x * y2, xz = q.x * z2;
        const float yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
        const float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
        Mat4 out;
        out.m[0] = 1.0f - (yy + zz);
        out.m[1] = xy + wz;
        out.m[2] = xz - wy;
        out.m[4] = xy - wz;
        out.m[5] = 1.0f - (xx + zz);
        out.m[6] = yz + wx;
        out.m[8] = xz + wy;
        out.m[9] = yz - wx;
        out.m[10] = 1.0f - (xx + yy);
        return out;
    }

    // T * R * S: scales, then rotates, then translates a column vector.
    [[nodiscard]] static Mat4 trs(Vec3 t, Quat r, Vec3 s) noexcept;

    // Right-handed view matrix, camera looking from eye toward target.
    [[nodiscard]] static Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up) noexcept {
        const Vec3 forward = normalize(target - eye); // view -Z
        const Vec3 right = normalize(cross(forward, up));
        const Vec3 real_up = cross(right, forward);
        Mat4 out;
        out.m[0] = right.x;
        out.m[4] = right.y;
        out.m[8] = right.z;
        out.m[1] = real_up.x;
        out.m[5] = real_up.y;
        out.m[9] = real_up.z;
        out.m[2] = -forward.x;
        out.m[6] = -forward.y;
        out.m[10] = -forward.z;
        out.m[12] = -dot(right, eye);
        out.m[13] = -dot(real_up, eye);
        out.m[14] = dot(forward, eye);
        return out;
    }

    // Right-handed perspective, depth mapped to 0..1 (Vulkan convention).
    [[nodiscard]] static Mat4 perspective(float fovy_radians, float aspect, float near_plane,
                                          float far_plane) noexcept {
        const float f = 1.0f / std::tan(0.5f * fovy_radians);
        Mat4 out;
        out.m[0] = f / aspect;
        out.m[5] = f;
        out.m[10] = far_plane / (near_plane - far_plane);
        out.m[11] = -1.0f;
        out.m[14] = (near_plane * far_plane) / (near_plane - far_plane);
        out.m[15] = 0.0f;
        return out;
    }

    friend Mat4 operator*(const Mat4& a, const Mat4& b) noexcept {
        Mat4 out;
#if HUE_SIMD_SSE
        const __m128 a0 = _mm_load_ps(a.m + 0);
        const __m128 a1 = _mm_load_ps(a.m + 4);
        const __m128 a2 = _mm_load_ps(a.m + 8);
        const __m128 a3 = _mm_load_ps(a.m + 12);
        for (int column = 0; column < 4; ++column) {
            // Per-element broadcasts measure faster than load + 4 shuffles
            // here: the shuffles all contend for the same execution port and
            // sit on the critical path when multiplies are chained.
            const __m128 b0 = _mm_set1_ps(b.m[column * 4 + 0]);
            const __m128 b1 = _mm_set1_ps(b.m[column * 4 + 1]);
            const __m128 b2 = _mm_set1_ps(b.m[column * 4 + 2]);
            const __m128 b3 = _mm_set1_ps(b.m[column * 4 + 3]);
            const __m128 result = _mm_add_ps(_mm_add_ps(_mm_mul_ps(a0, b0), _mm_mul_ps(a1, b1)),
                                             _mm_add_ps(_mm_mul_ps(a2, b2), _mm_mul_ps(a3, b3)));
            _mm_store_ps(out.m + column * 4, result);
        }
#else
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += a.m[k * 4 + row] * b.m[column * 4 + k];
                }
                out.m[column * 4 + row] = sum;
            }
        }
#endif
        return out;
    }

    friend Vec4 operator*(const Mat4& a, Vec4 v) noexcept {
#if HUE_SIMD_SSE
        const __m128 vec = _mm_load_ps(&v.x);
        const __m128 result = _mm_add_ps(
            _mm_add_ps(
                _mm_mul_ps(_mm_load_ps(a.m + 0), _mm_shuffle_ps(vec, vec, _MM_SHUFFLE(0, 0, 0, 0))),
                _mm_mul_ps(_mm_load_ps(a.m + 4), _mm_shuffle_ps(vec, vec, _MM_SHUFFLE(1, 1, 1, 1)))),
            _mm_add_ps(
                _mm_mul_ps(_mm_load_ps(a.m + 8), _mm_shuffle_ps(vec, vec, _MM_SHUFFLE(2, 2, 2, 2))),
                _mm_mul_ps(_mm_load_ps(a.m + 12),
                           _mm_shuffle_ps(vec, vec, _MM_SHUFFLE(3, 3, 3, 3)))));
        Vec4 out;
        _mm_store_ps(&out.x, result);
        return out;
#else
        Vec4 out;
        out.x = a.m[0] * v.x + a.m[4] * v.y + a.m[8] * v.z + a.m[12] * v.w;
        out.y = a.m[1] * v.x + a.m[5] * v.y + a.m[9] * v.z + a.m[13] * v.w;
        out.z = a.m[2] * v.x + a.m[6] * v.y + a.m[10] * v.z + a.m[14] * v.w;
        out.w = a.m[3] * v.x + a.m[7] * v.y + a.m[11] * v.z + a.m[15] * v.w;
        return out;
#endif
    }

    // Affine point transform (w = 1, no perspective divide).
    [[nodiscard]] Vec3 transform_point(Vec3 p) const noexcept {
        const Vec4 r = *this * Vec4{p.x, p.y, p.z, 1.0f};
        return {r.x, r.y, r.z};
    }

    // Direction transform (w = 0: rotation/scale only, no translation).
    [[nodiscard]] Vec3 transform_vector(Vec3 v) const noexcept {
        const Vec4 r = *this * Vec4{v.x, v.y, v.z, 0.0f};
        return {r.x, r.y, r.z};
    }

    [[nodiscard]] Mat4 transposed() const noexcept {
        Mat4 out;
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                out.m[row * 4 + column] = m[column * 4 + row];
            }
        }
        return out;
    }

    // General 4x4 inverse (cofactor expansion). Returns identity for a
    // singular matrix; engine matrices (TRS, view, proj) are never singular.
    [[nodiscard]] Mat4 inverted() const noexcept;

    // Inverse of a rigid transform (rotation + translation, NO scale):
    // M^-1 = [R^T | -R^T t]. ~12 multiplies vs ~200 flops for the general
    // cofactor inverse. This is the hot path for view matrices and bones.
    [[nodiscard]] Mat4 inverted_rigid() const noexcept {
        Mat4 out;
        out.m[0] = m[0];
        out.m[1] = m[4];
        out.m[2] = m[8];
        out.m[4] = m[1];
        out.m[5] = m[5];
        out.m[6] = m[9];
        out.m[8] = m[2];
        out.m[9] = m[6];
        out.m[10] = m[10];
        out.m[12] = -(out.m[0] * m[12] + out.m[4] * m[13] + out.m[8] * m[14]);
        out.m[13] = -(out.m[1] * m[12] + out.m[5] * m[13] + out.m[9] * m[14]);
        out.m[14] = -(out.m[2] * m[12] + out.m[6] * m[13] + out.m[10] * m[14]);
        return out;
    }
};

// Direct composition instead of translation(t) * rotation(r) * scaling(s):
// the two full 4x4 multiplies cost ~128 mul + ~96 add; scaling the rotation
// columns in place and writing the translation costs 9 multiplies. This is
// per-bone, per-frame work in the animation pipeline.
inline Mat4 Mat4::trs(Vec3 t, Quat r, Vec3 s) noexcept {
    Mat4 out = Mat4::rotation(r);
    out.m[0] *= s.x;
    out.m[1] *= s.x;
    out.m[2] *= s.x;
    out.m[4] *= s.y;
    out.m[5] *= s.y;
    out.m[6] *= s.y;
    out.m[8] *= s.z;
    out.m[9] *= s.z;
    out.m[10] *= s.z;
    out.m[12] = t.x;
    out.m[13] = t.y;
    out.m[14] = t.z;
    return out;
}

inline Mat4 Mat4::inverted() const noexcept {
    const float* a = m;
    float inv[16];

    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] +
             a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] -
             a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] +
             a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] -
              a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] -
             a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] +
             a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] -
             a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] +
              a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] +
             a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] -
             a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] +
              a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] -
              a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] -
             a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] +
             a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] -
              a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] +
              a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

    const float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (std::fabs(det) <= 1e-12f) {
        return Mat4::identity();
    }
    const float inv_det = 1.0f / det;
    Mat4 out;
    for (int i = 0; i < 16; ++i) {
        out.m[i] = inv[i] * inv_det;
    }
    return out;
}

} // namespace hue
