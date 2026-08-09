// engine/asset/src/gltf_loader.cpp
//
// Validated glTF import. cgltf parses; we decide what is trustworthy.
// Order of defenses: size cap -> cgltf_parse -> cgltf_load_buffers with no
// base path (embedded data only) -> cgltf_validate (accessor ranges vs
// buffer sizes) -> our caps and format checks -> per-index bounds check.

#include "hue/asset/gltf_loader.h"

#include "hue/core/checked_math.h"
#include "hue/core/log.h"
#include "hue/core/memory.h"

#include <cgltf.h>

#include <cstdio>
#include <cstring>

namespace hue::asset {

namespace {

void* cgltf_alloc_thunk(void* user, cgltf_size size) {
    (void)user;
    auto allocation = heap_allocate(size > 0 ? size : 1, alignof(std::max_align_t),
                                    MemoryTag::kAssets);
    return allocation ? allocation.value() : nullptr;
}

void cgltf_free_thunk(void* user, void* memory) {
    (void)user;
    const auto freed = heap_free(memory);
    (void)freed;
}

struct DataGuard {
    cgltf_data* data = nullptr;
    ~DataGuard() {
        if (data != nullptr) {
            cgltf_free(data);
        }
    }
};

// Attribute accessor must match the exact component layout we upload;
// anything else is rejected rather than converted-and-hoped.
[[nodiscard]] bool accessor_is(const cgltf_accessor* accessor, cgltf_type type,
                               cgltf_size expected_count) {
    return accessor != nullptr && accessor->type == type &&
           accessor->component_type == cgltf_component_type_r_32f && !accessor->is_sparse &&
           accessor->count == expected_count;
}

[[nodiscard]] Result<void> append_primitive(StaticMeshData& out, const cgltf_primitive& primitive,
                                            std::uint32_t& total_vertices,
                                            std::uint32_t& total_indices) {
    if (primitive.type != cgltf_primitive_type_triangles) {
        HUE_LOG_WARN("gltf: non-triangle primitive rejected");
        return ErrorCode::kUnsupported;
    }

    const cgltf_accessor* positions = nullptr;
    const cgltf_accessor* normals = nullptr;
    const cgltf_accessor* uvs = nullptr;
    for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
        const cgltf_attribute& attribute = primitive.attributes[a];
        if (attribute.type == cgltf_attribute_type_position && attribute.index == 0) {
            positions = attribute.data;
        } else if (attribute.type == cgltf_attribute_type_normal && attribute.index == 0) {
            normals = attribute.data;
        } else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) {
            uvs = attribute.data;
        }
    }

    if (positions == nullptr || positions->type != cgltf_type_vec3 ||
        positions->component_type != cgltf_component_type_r_32f || positions->is_sparse) {
        return ErrorCode::kCorruptData;
    }
    const cgltf_size vertex_count = positions->count;
    if (vertex_count == 0 || vertex_count > kGltfMaxVertices - total_vertices) {
        return ErrorCode::kCorruptData;
    }
    if (normals != nullptr && !accessor_is(normals, cgltf_type_vec3, vertex_count)) {
        return ErrorCode::kCorruptData;
    }
    if (uvs != nullptr && !accessor_is(uvs, cgltf_type_vec2, vertex_count)) {
        return ErrorCode::kCorruptData;
    }

    cgltf_size index_count = vertex_count; // no index accessor: sequential triangles
    if (primitive.indices != nullptr) {
        if (primitive.indices->type != cgltf_type_scalar || primitive.indices->is_sparse) {
            return ErrorCode::kCorruptData;
        }
        index_count = primitive.indices->count;
    }
    if (index_count == 0 || index_count % 3 != 0 ||
        index_count > kGltfMaxIndices - total_indices) {
        return ErrorCode::kCorruptData;
    }

    // ---- vertices
    const std::uint32_t vertex_offset = total_vertices;
    if (!out.vertices.reserve(total_vertices + vertex_count)) {
        return ErrorCode::kOutOfMemory;
    }
    for (cgltf_size v = 0; v < vertex_count; ++v) {
        StaticVertex vertex{};
        cgltf_float value[3] = {};
        if (cgltf_accessor_read_float(positions, v, value, 3) == 0) {
            return ErrorCode::kCorruptData;
        }
        vertex.position = {value[0], value[1], value[2]};
        if (normals != nullptr) {
            if (cgltf_accessor_read_float(normals, v, value, 3) == 0) {
                return ErrorCode::kCorruptData;
            }
            vertex.normal = {value[0], value[1], value[2]};
        } else {
            vertex.normal = {0.0f, 1.0f, 0.0f};
        }
        if (uvs != nullptr) {
            if (cgltf_accessor_read_float(uvs, v, value, 2) == 0) {
                return ErrorCode::kCorruptData;
            }
            vertex.uv = {value[0], value[1]};
        }
        if (!out.vertices.push_back(vertex)) {
            return ErrorCode::kOutOfMemory;
        }
    }

    // ---- indices (every value bounds-checked against this primitive)
    const std::uint32_t first_index = total_indices;
    if (!out.indices.reserve(total_indices + index_count)) {
        return ErrorCode::kOutOfMemory;
    }
    for (cgltf_size i = 0; i < index_count; ++i) {
        cgltf_size index = i;
        if (primitive.indices != nullptr) {
            index = cgltf_accessor_read_index(primitive.indices, i);
        }
        if (index >= vertex_count) {
            HUE_LOG_WARN("gltf: index out of range (%zu >= %zu)", static_cast<std::size_t>(index),
                         static_cast<std::size_t>(vertex_count));
            return ErrorCode::kCorruptData;
        }
        if (!out.indices.push_back(static_cast<std::uint32_t>(index))) {
            return ErrorCode::kOutOfMemory;
        }
    }

    MeshPrimitive mesh_primitive;
    mesh_primitive.index_count = static_cast<std::uint32_t>(index_count);
    mesh_primitive.first_index = first_index;
    mesh_primitive.vertex_offset = static_cast<std::int32_t>(vertex_offset);
    if (!out.primitives.push_back(mesh_primitive)) {
        return ErrorCode::kOutOfMemory;
    }

    total_vertices += static_cast<std::uint32_t>(vertex_count);
    total_indices += static_cast<std::uint32_t>(index_count);
    return {};
}

// Local-space AABB of one primitive, from the vertices just appended.
[[nodiscard]] Aabb primitive_local_bounds(const StaticMeshData& data,
                                          const MeshPrimitive& primitive,
                                          std::uint32_t vertex_count) {
    const auto first = static_cast<std::uint32_t>(primitive.vertex_offset);
    Aabb bounds{data.vertices[first].position, data.vertices[first].position};
    for (std::uint32_t v = 1; v < vertex_count; ++v) {
        bounds = bounds.expanded_to_include(data.vertices[first + v].position);
    }
    return bounds;
}

} // namespace

Result<StaticMeshData> load_gltf(const void* bytes, std::size_t size) noexcept {
    if (bytes == nullptr || size == 0 || size > kGltfMaxFileBytes) {
        return ErrorCode::kCorruptData;
    }

    cgltf_options options{};
    options.memory.alloc_func = &cgltf_alloc_thunk;
    options.memory.free_func = &cgltf_free_thunk;

    DataGuard guard;
    if (cgltf_parse(&options, bytes, size, &guard.data) != cgltf_result_success) {
        return ErrorCode::kCorruptData;
    }
    // No base path: GLB bin chunks and data URIs resolve, references to
    // external files fail. The fuzz target must never touch the disk.
    if (cgltf_load_buffers(&options, guard.data, nullptr) != cgltf_result_success) {
        return ErrorCode::kUnsupported;
    }
    if (cgltf_validate(guard.data) != cgltf_result_success) {
        return ErrorCode::kCorruptData;
    }

    const cgltf_data& data = *guard.data;

    StaticMeshData out;
    std::uint32_t total_vertices = 0;
    std::uint32_t total_indices = 0;
    std::uint32_t total_primitives = 0;
    std::uint32_t total_instances = 0;
    bool bounds_valid = false;

    for (cgltf_size n = 0; n < data.nodes_count; ++n) {
        const cgltf_node& node = data.nodes[n];
        if (node.mesh == nullptr) {
            continue;
        }
        if (node.mesh->primitives_count > kGltfMaxPrimitives - total_primitives ||
            node.mesh->primitives_count > kGltfMaxInstances - total_instances) {
            return ErrorCode::kCorruptData;
        }

        Mat4 world;
        cgltf_node_transform_world(&node, world.m); // column-major, matches Mat4

        for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p) {
            // Shared meshes are deliberately re-appended per node: simple,
            // and instancing proper arrives with the ECS in Week 8.
            const std::uint32_t primitive_index = static_cast<std::uint32_t>(out.primitives.size());
            const std::uint32_t vertices_before = total_vertices;
            const auto appended =
                append_primitive(out, node.mesh->primitives[p], total_vertices, total_indices);
            if (!appended) {
                return appended.error();
            }
            ++total_primitives;

            MeshInstance instance;
            instance.primitive_index = primitive_index;
            instance.transform = world;
            if (!out.instances.push_back(instance)) {
                return ErrorCode::kOutOfMemory;
            }
            ++total_instances;

            const Aabb local = primitive_local_bounds(out, out.primitives[primitive_index],
                                                      total_vertices - vertices_before);
            const Aabb world_bounds = local.transformed(world);
            out.bounds = bounds_valid ? out.bounds.merged(world_bounds) : world_bounds;
            bounds_valid = true;
        }
    }

    if (total_primitives == 0) {
        return ErrorCode::kCorruptData; // nothing drawable is not a usable mesh asset
    }

    HUE_LOG_INFO("gltf: loaded %u primitives, %u instances, %u vertices, %u indices",
                 total_primitives, total_instances, total_vertices, total_indices);
    return out;
}

Result<StaticMeshData> load_gltf_file(const char* path) noexcept {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return ErrorCode::kNotFound;
    }
    std::fseek(file, 0, SEEK_END);
    const long file_size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (file_size <= 0 || static_cast<std::size_t>(file_size) > kGltfMaxFileBytes) {
        std::fclose(file);
        return ErrorCode::kCorruptData;
    }

    auto allocation = heap_allocate(static_cast<std::size_t>(file_size),
                                    alignof(std::max_align_t), MemoryTag::kAssets);
    if (!allocation) {
        std::fclose(file);
        return allocation.error();
    }
    void* buffer = allocation.value();
    const std::size_t read = std::fread(buffer, 1, static_cast<std::size_t>(file_size), file);
    std::fclose(file);

    Result<StaticMeshData> result = read == static_cast<std::size_t>(file_size)
                                        ? load_gltf(buffer, read)
                                        : Result<StaticMeshData>(ErrorCode::kCorruptData);
    const auto freed = heap_free(buffer);
    (void)freed;
    return result;
}

} // namespace hue::asset
