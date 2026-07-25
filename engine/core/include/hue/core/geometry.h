// engine/core/include/hue/core/geometry.h
//
// Culling primitives (Week 3 spec): axis-aligned bounding boxes and a
// 6-plane frustum extracted from a view-projection matrix
// (Gribb/Hartmann method, adapted for 0..1 clip depth).

#pragma once

#include "hue/core/math.h"

namespace hue {

// ---------------------------------------------------------------- Aabb

struct Aabb {
    Vec3 min_point{};
    Vec3 max_point{};

    [[nodiscard]] static constexpr Aabb from_center_extents(Vec3 center, Vec3 half_extents) noexcept {
        return {center - half_extents, center + half_extents};
    }

    [[nodiscard]] constexpr Vec3 center() const noexcept {
        return (min_point + max_point) * 0.5f;
    }
    [[nodiscard]] constexpr Vec3 half_extents() const noexcept {
        return (max_point - min_point) * 0.5f;
    }

    [[nodiscard]] constexpr Aabb expanded_to_include(Vec3 p) const noexcept {
        return {min(min_point, p), max(max_point, p)};
    }
    [[nodiscard]] constexpr Aabb merged(const Aabb& other) const noexcept {
        return {min(min_point, other.min_point), max(max_point, other.max_point)};
    }

    [[nodiscard]] constexpr bool contains(Vec3 p) const noexcept {
        return p.x >= min_point.x && p.x <= max_point.x && p.y >= min_point.y &&
               p.y <= max_point.y && p.z >= min_point.z && p.z <= max_point.z;
    }
    [[nodiscard]] constexpr bool intersects(const Aabb& other) const noexcept {
        return min_point.x <= other.max_point.x && max_point.x >= other.min_point.x &&
               min_point.y <= other.max_point.y && max_point.y >= other.min_point.y &&
               min_point.z <= other.max_point.z && max_point.z >= other.min_point.z;
    }

    // Transform by an affine matrix: new box is the tight AABB of the
    // transformed corners (Arvo's method: |M| applied to extents).
    [[nodiscard]] Aabb transformed(const Mat4& transform) const noexcept {
        const Vec3 c = center();
        const Vec3 e = half_extents();
        const Vec3 new_center = transform.transform_point(c);
        Vec3 new_extents{};
        for (std::size_t row = 0; row < 3; ++row) {
            float sum = 0.0f;
            sum += std::fabs(transform.at(row, 0)) * e.x;
            sum += std::fabs(transform.at(row, 1)) * e.y;
            sum += std::fabs(transform.at(row, 2)) * e.z;
            if (row == 0) {
                new_extents.x = sum;
            } else if (row == 1) {
                new_extents.y = sum;
            } else {
                new_extents.z = sum;
            }
        }
        return from_center_extents(new_center, new_extents);
    }
};

// ---------------------------------------------------------------- Plane

// Plane stored as (normal, d): a point p is inside when dot(normal, p) + d >= 0.
struct Plane {
    Vec3 normal{};
    float d = 0.0f;

    [[nodiscard]] float signed_distance(Vec3 p) const noexcept {
        return dot(normal, p) + d;
    }
};

// ---------------------------------------------------------------- Frustum

class Frustum {
public:
    enum PlaneIndex : std::size_t {
        kLeft = 0,
        kRight,
        kBottom,
        kTop,
        kNear,
        kFar,
        kPlaneCount,
    };

    // Extract world-space planes from a view-projection matrix. Rows of the
    // combined matrix follow Gribb/Hartmann; near plane uses row 2 alone
    // because our clip depth range is 0..1 (Vulkan), not -1..1.
    [[nodiscard]] static Frustum from_view_projection(const Mat4& view_projection) noexcept {
        const auto matrix_row = [&view_projection](std::size_t row) noexcept {
            return Vec4{view_projection.at(row, 0), view_projection.at(row, 1),
                        view_projection.at(row, 2), view_projection.at(row, 3)};
        };
        const Vec4 row0 = matrix_row(0);
        const Vec4 row1 = matrix_row(1);
        const Vec4 row2 = matrix_row(2);
        const Vec4 row3 = matrix_row(3);

        Frustum out;
        out.set_plane(kLeft, row3 + row0);
        out.set_plane(kRight, row3 - row0);
        out.set_plane(kBottom, row3 + row1);
        out.set_plane(kTop, row3 - row1);
        out.set_plane(kNear, row2);
        out.set_plane(kFar, row3 - row2);
        return out;
    }

    [[nodiscard]] const Plane& plane(PlaneIndex index) const noexcept {
        return m_planes[index];
    }

    [[nodiscard]] bool contains_point(Vec3 p) const noexcept {
        for (const Plane& pl : m_planes) {
            if (pl.signed_distance(p) < 0.0f) {
                return false;
            }
        }
        return true;
    }

    // Conservative AABB test using the positive-vertex trick: box is culled
    // only when its most-positive corner is behind some plane.
    [[nodiscard]] bool intersects(const Aabb& box) const noexcept {
        for (const Plane& pl : m_planes) {
            const Vec3 positive_vertex{
                pl.normal.x >= 0.0f ? box.max_point.x : box.min_point.x,
                pl.normal.y >= 0.0f ? box.max_point.y : box.min_point.y,
                pl.normal.z >= 0.0f ? box.max_point.z : box.min_point.z,
            };
            if (pl.signed_distance(positive_vertex) < 0.0f) {
                return false;
            }
        }
        return true;
    }

private:
    void set_plane(std::size_t index, Vec4 row) noexcept {
        const Vec3 normal{row.x, row.y, row.z};
        const float len = length(normal);
        if (len > kEpsilon) {
            m_planes[index] = {normal / len, row.w / len};
        } else {
            m_planes[index] = {};
        }
    }

    Plane m_planes[kPlaneCount]{};
};

} // namespace hue
