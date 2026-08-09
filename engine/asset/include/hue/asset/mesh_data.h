//engine/asset/include/hue/asset/mesh_data.h

#pragma once

#include <cstdint>

#include "hue/core/array.h"
#include "hue/core/geometry.h"
#include "hue/core/math.h"
#include "hue/core/memory.h"

namespace hue::asset {

    struct StaticVertex {
        Vec3 position;
        Vec3 normal;
        Vec2 uv;
    };

    struct MeshPrimitive {
        std::uint32_t index_count=0;
        std::uint32_t first_index=0;
        std::int32_t vertex_offset=0;
    };

    struct MeshInstance {
        std::uint32_t primitive_index=0;
        Mat4 transform;
    };

    struct StaticMeshData {
        Array<StaticVertex> vertices{MemoryTag::kAssets};
        Array<std::uint32_t> indices{MemoryTag::kAssets};
        Array<MeshPrimitive> primitives{MemoryTag::kAssets};
        Array<MeshInstance> instances{MemoryTag::kAssets};

        Aabb bounds;
    };

} // namespace hue::asset
