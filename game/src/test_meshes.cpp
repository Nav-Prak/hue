// game/src/test_meshes.cpp

#include "test_meshes.h"

#include "hue/asset/gltf_loader.h"
#include "hue/core/log.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using hue::Mat4;
using hue::Quat;
using hue::Vec3;
using hue::asset::MeshInstance;
using hue::asset::MeshPrimitive;
using hue::asset::StaticMeshData;
using hue::asset::StaticVertex;

// Appends one quad face: corners span center +/- u/2 +/- v/2 with normal
// cross(u, v); counter-clockwise seen from the normal side.
[[nodiscard]] hue::Result<void> append_face(StaticMeshData& data, Vec3 center, Vec3 u, Vec3 v,
                                            float uv_scale) {
    const Vec3 normal = hue::normalize(hue::cross(u, v));
    const auto base = static_cast<std::uint32_t>(data.vertices.size());

    const Vec3 corners[4] = {
        center - u * 0.5f - v * 0.5f,
        center + u * 0.5f - v * 0.5f,
        center + u * 0.5f + v * 0.5f,
        center - u * 0.5f + v * 0.5f,
    };
    const float uv[4][2] = {{0.0f, 0.0f}, {uv_scale, 0.0f}, {uv_scale, uv_scale}, {0.0f, uv_scale}};

    for (int c = 0; c < 4; ++c) {
        StaticVertex vertex;
        vertex.position = corners[c];
        vertex.normal = normal;
        vertex.uv = {uv[c][0], uv[c][1]};
        if (!data.vertices.push_back(vertex)) {
            return hue::ErrorCode::kOutOfMemory;
        }
    }
    const std::uint32_t quad_indices[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
    for (const std::uint32_t index : quad_indices) {
        if (!data.indices.push_back(index)) {
            return hue::ErrorCode::kOutOfMemory;
        }
    }
    return {};
}

// Six faces of a unit cube centered at the origin.
[[nodiscard]] hue::Result<void> append_cube_faces(StaticMeshData& data, float uv_scale) {
    const Vec3 x{1.0f, 0.0f, 0.0f};
    const Vec3 y{0.0f, 1.0f, 0.0f};
    const Vec3 z{0.0f, 0.0f, 1.0f};
    struct Face {
        Vec3 center, u, v;
    };
    const Face faces[6] = {
        {z * 0.5f, x, y},          // +Z
        {z * -0.5f, x * -1.0f, y}, // -Z
        {x * 0.5f, z * -1.0f, y},  // +X
        {x * -0.5f, z, y},         // -X
        {y * 0.5f, x, z * -1.0f},  // +Y
        {y * -0.5f, x, z},         // -Y
    };
    for (const Face& face : faces) {
        const auto appended = append_face(data, face.center, face.u, face.v, uv_scale);
        if (!appended) {
            return appended.error();
        }
    }
    return {};
}

[[nodiscard]] hue::Result<void> close_primitive(StaticMeshData& data, std::uint32_t& first_index,
                                                std::uint32_t& first_vertex,
                                                std::int32_t material_index) {
    MeshPrimitive primitive;
    primitive.first_index = first_index;
    primitive.index_count = static_cast<std::uint32_t>(data.indices.size()) - first_index;
    primitive.vertex_offset = 0; // indices in this builder are absolute
    primitive.material_index = material_index;
    if (!data.primitives.push_back(primitive)) {
        return hue::ErrorCode::kOutOfMemory;
    }
    first_index = static_cast<std::uint32_t>(data.indices.size());
    first_vertex = static_cast<std::uint32_t>(data.vertices.size());
    return {};
}

// Procedural 256x256 checker albedo (Week 6: the in-shader checker moved
// into a real sampled texture with a full mip chain).
[[nodiscard]] hue::Result<void> append_checker_texture(StaticMeshData& data) {
    hue::asset::TextureData texture;
    texture.width = 256;
    texture.height = 256;
    texture.srgb = true;

    std::uint8_t row[256 * 4];
    for (std::uint32_t y = 0; y < 256; ++y) {
        for (std::uint32_t x = 0; x < 256; ++x) {
            const bool light = ((x / 32) + (y / 32)) % 2 == 0;
            row[x * 4 + 0] = light ? 168 : 132;
            row[x * 4 + 1] = light ? 172 : 138;
            row[x * 4 + 2] = light ? 182 : 152;
            row[x * 4 + 3] = 255;
        }
        if (!texture.pixels.append(row, sizeof(row))) {
            return hue::ErrorCode::kOutOfMemory;
        }
    }
    if (!data.textures.push_back(std::move(texture))) {
        return hue::ErrorCode::kOutOfMemory;
    }
    return {};
}

} // namespace

hue::Result<StaticMeshData> build_ground_scene() {
    StaticMeshData data;
    std::uint32_t first_index = 0;
    std::uint32_t first_vertex = 0;

    // Material 0: checker-textured ground. Material 1: untextured painted
    // metal for the boxes (factors only, white fallback texture).
    auto step = append_checker_texture(data);
    if (!step) {
        return step.error();
    }
    hue::asset::MaterialData ground_material;
    ground_material.base_color_texture = 0;
    ground_material.metallic_factor = 0.0f;
    ground_material.roughness_factor = 0.9f;
    if (!data.materials.push_back(ground_material)) {
        return hue::ErrorCode::kOutOfMemory;
    }
    hue::asset::MaterialData box_material;
    box_material.base_color_factor = {0.32f, 0.42f, 0.68f, 1.0f};
    box_material.metallic_factor = 0.85f;
    box_material.roughness_factor = 0.35f;
    if (!data.materials.push_back(box_material)) {
        return hue::ErrorCode::kOutOfMemory;
    }

    // Primitive 0: 40x40m ground plane at y=0.
    step = append_face(data, {}, Vec3{40.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, -40.0f}, 16.0f);
    if (!step) {
        return step.error();
    }
    step = close_primitive(data, first_index, first_vertex, 0);
    if (!step) {
        return step.error();
    }

    // Primitive 1: unit cube, instanced in a ring plus a center pillar.
    step = append_cube_faces(data, 1.0f);
    if (!step) {
        return step.error();
    }
    step = close_primitive(data, first_index, first_vertex, 1);
    if (!step) {
        return step.error();
    }

    MeshInstance ground;
    ground.primitive_index = 0;
    ground.transform = Mat4::identity();
    if (!data.instances.push_back(ground)) {
        return hue::ErrorCode::kOutOfMemory;
    }

    constexpr int kRingCount = 8;
    constexpr float kRingRadius = 6.0f;
    for (int i = 0; i < kRingCount; ++i) {
        const float angle = hue::kTwoPi * static_cast<float>(i) / kRingCount;
        const float height = 1.0f + 0.5f * static_cast<float>(i % 3);
        MeshInstance cube;
        cube.primitive_index = 1;
        cube.transform = Mat4::trs(
            {kRingRadius * std::cos(angle), height * 0.5f, kRingRadius * std::sin(angle)},
            Quat::from_axis_angle({0.0f, 1.0f, 0.0f}, angle), {1.0f, height, 1.0f});
        if (!data.instances.push_back(cube)) {
            return hue::ErrorCode::kOutOfMemory;
        }
    }
    MeshInstance pillar;
    pillar.primitive_index = 1;
    pillar.transform = Mat4::trs({0.0f, 1.5f, 0.0f}, Quat{}, {1.5f, 3.0f, 1.5f});
    if (!data.instances.push_back(pillar)) {
        return hue::ErrorCode::kOutOfMemory;
    }

    data.bounds = hue::Aabb{{-20.0f, 0.0f, -20.0f}, {20.0f, 3.0f, 20.0f}};
    return data;
}

// ---------------------------------------------------------------- glTF cube
//
// Builds a complete GLB (JSON chunk + BIN chunk) for the same unit cube and
// feeds it to hue::asset::load_gltf. If the validated import path ever
// regresses, the cube disappears from the scene and the log says why.

hue::Result<StaticMeshData> build_gltf_cube() {
    StaticMeshData cube;
    const auto built = append_cube_faces(cube, 1.0f);
    if (!built) {
        return built.error();
    }

    const std::uint32_t vertex_count = static_cast<std::uint32_t>(cube.vertices.size());
    const std::uint32_t index_count = static_cast<std::uint32_t>(cube.indices.size());
    const std::uint32_t positions_bytes = vertex_count * 12;
    const std::uint32_t normals_bytes = vertex_count * 12;
    const std::uint32_t uvs_bytes = vertex_count * 8;
    const std::uint32_t indices_bytes = index_count * 4;
    const std::uint32_t bin_bytes = positions_bytes + normals_bytes + uvs_bytes + indices_bytes;

    char json[2048];
    const int json_length = std::snprintf(
        json, sizeof(json),
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,"
        "\"TEXCOORD_0\":2},\"indices\":3,\"material\":0}]}],"
        "\"materials\":[{\"pbrMetallicRoughness\":{"
        "\"baseColorFactor\":[0.85,0.45,0.15,1.0],"
        "\"metallicFactor\":0.15,\"roughnessFactor\":0.45}}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\","
        "\"min\":[-0.5,-0.5,-0.5],\"max\":[0.5,0.5,0.5]},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":%u,\"type\":\"VEC2\"},"
        "{\"bufferView\":3,\"componentType\":5125,\"count\":%u,\"type\":\"SCALAR\"}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%u},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}],"
        "\"buffers\":[{\"byteLength\":%u}]}",
        vertex_count, vertex_count, vertex_count, index_count, positions_bytes, positions_bytes,
        normals_bytes, positions_bytes + normals_bytes, uvs_bytes,
        positions_bytes + normals_bytes + uvs_bytes, indices_bytes, bin_bytes);
    if (json_length <= 0 || static_cast<std::size_t>(json_length) >= sizeof(json)) {
        return hue::ErrorCode::kUnknown;
    }
    const std::uint32_t json_padded = (static_cast<std::uint32_t>(json_length) + 3u) & ~3u;
    const std::uint32_t bin_padded = (bin_bytes + 3u) & ~3u;
    const std::uint32_t total = 12 + 8 + json_padded + 8 + bin_padded;

    hue::Array<std::uint8_t> glb{hue::MemoryTag::kAssets};
    if (!glb.reserve(total)) {
        return hue::ErrorCode::kOutOfMemory;
    }
    auto push_u32 = [&glb](std::uint32_t value) {
        std::uint8_t bytes[4];
        std::memcpy(bytes, &value, 4);
        for (int b = 0; b < 4; ++b) {
            (void)glb.push_back(bytes[b]);
        }
    };
    auto push_bytes = [&glb](const void* data_ptr, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data_ptr);
        for (std::size_t b = 0; b < size; ++b) {
            (void)glb.push_back(bytes[b]);
        }
    };

    push_u32(0x46546C67u); // 'glTF'
    push_u32(2u);
    push_u32(total);
    push_u32(json_padded);
    push_u32(0x4E4F534Au); // 'JSON'
    push_bytes(json, static_cast<std::size_t>(json_length));
    for (std::uint32_t p = static_cast<std::uint32_t>(json_length); p < json_padded; ++p) {
        (void)glb.push_back(static_cast<std::uint8_t>(' '));
    }
    push_u32(bin_padded);
    push_u32(0x004E4942u); // 'BIN\0'
    for (std::uint32_t v = 0; v < vertex_count; ++v) {
        push_bytes(&cube.vertices[v].position, 12);
    }
    for (std::uint32_t v = 0; v < vertex_count; ++v) {
        push_bytes(&cube.vertices[v].normal, 12);
    }
    for (std::uint32_t v = 0; v < vertex_count; ++v) {
        push_bytes(&cube.vertices[v].uv, 8);
    }
    push_bytes(cube.indices.data(), indices_bytes);
    for (std::uint32_t p = bin_bytes; p < bin_padded; ++p) {
        (void)glb.push_back(0u);
    }

    if (glb.size() != total) {
        return hue::ErrorCode::kUnknown; // builder bug, not input data
    }
    return hue::asset::load_gltf(glb.data(), glb.size());
}
