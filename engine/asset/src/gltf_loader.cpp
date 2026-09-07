// engine/asset/src/gltf_loader.cpp
//
// Validated glTF import. cgltf parses; we decide what is trustworthy.
// Order of defenses: size cap -> cgltf_parse -> cgltf_load_buffers with no
// base path (embedded data only) -> cgltf_validate (accessor ranges vs
// buffer sizes) -> our caps and format checks -> per-index bounds check.
// Week 6 widens the boundary: PNG/JPEG decode (stb_image, embedded buffer
// views only, dimension + decoded-byte caps), material factors clamped,
// joint indices bounds-checked and remapped to topological order, weights
// renormalized, keyframe tracks checked finite and non-decreasing.

#include "hue/asset/gltf_loader.h"

#include "hue/core/checked_math.h"
#include "hue/core/log.h"
#include "hue/core/memory.h"

#include <cgltf.h>

#define STBI_NO_STDIO
#include <stb_image.h>

#include <cmath>
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

// Shared front door: size cap, parse, embedded-only buffer resolve,
// cgltf_validate. Every loader entry point funnels through here.
[[nodiscard]] Result<void> parse_and_validate(DataGuard& guard, const void* bytes,
                                              std::size_t size) {
    if (bytes == nullptr || size == 0 || size > kGltfMaxFileBytes) {
        return ErrorCode::kCorruptData;
    }

    cgltf_options options{};
    options.memory.alloc_func = &cgltf_alloc_thunk;
    options.memory.free_func = &cgltf_free_thunk;

    if (cgltf_parse(&options, bytes, size, &guard.data) != cgltf_result_success) {
        return ErrorCode::kCorruptData;
    }
    // No base path: GLB bin chunks and data URIs resolve, references to
    // external files fail. The fuzz targets must never touch the disk.
    if (cgltf_load_buffers(&options, guard.data, nullptr) != cgltf_result_success) {
        return ErrorCode::kUnsupported;
    }
    if (cgltf_validate(guard.data) != cgltf_result_success) {
        return ErrorCode::kCorruptData;
    }
    return {};
}

// Attribute accessor must match the exact component layout we upload;
// anything else is rejected rather than converted-and-hoped.
[[nodiscard]] bool accessor_is(const cgltf_accessor* accessor, cgltf_type type,
                               cgltf_size expected_count) {
    return accessor != nullptr && accessor->type == type &&
           accessor->component_type == cgltf_component_type_r_32f && !accessor->is_sparse &&
           accessor->count == expected_count;
}

[[nodiscard]] float clamped01(float value) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

[[nodiscard]] std::int32_t texture_index_of(const cgltf_data& data, const cgltf_texture* texture) {
    if (texture == nullptr) {
        return -1;
    }
    return static_cast<std::int32_t>(texture - data.textures);
}

// ------------------------------------------------------------ textures

// Decodes every texture in the file. Only embedded buffer-view images are
// accepted (GLB standard practice); URI-referenced images would mean disk
// or base64-in-image paths we deliberately keep outside the boundary.
[[nodiscard]] Result<void> load_textures(const cgltf_data& data, Array<TextureData>& out) {
    if (data.textures_count > kGltfMaxTextures) {
        HUE_LOG_WARN("gltf: too many textures (%zu > %u)",
                     static_cast<std::size_t>(data.textures_count), kGltfMaxTextures);
        return ErrorCode::kCorruptData;
    }

    // sRGB flags come from usage: base color samples are color (sRGB),
    // metallic-roughness samples are data (linear).
    bool srgb_flags[kGltfMaxTextures] = {};
    for (cgltf_size m = 0; m < data.materials_count; ++m) {
        const cgltf_material& material = data.materials[m];
        if (!material.has_pbr_metallic_roughness) {
            continue;
        }
        const std::int32_t base_color =
            texture_index_of(data, material.pbr_metallic_roughness.base_color_texture.texture);
        if (base_color >= 0 && static_cast<cgltf_size>(base_color) < data.textures_count) {
            srgb_flags[base_color] = true;
        }
    }

    std::size_t decoded_total = 0;
    for (cgltf_size t = 0; t < data.textures_count; ++t) {
        const cgltf_image* image = data.textures[t].image;
        if (image == nullptr) {
            return ErrorCode::kCorruptData;
        }
        if (image->buffer_view == nullptr) {
            HUE_LOG_WARN("gltf: URI-referenced image rejected (embedded only)");
            return ErrorCode::kUnsupported;
        }
        const cgltf_buffer_view& view = *image->buffer_view;
        if (view.buffer == nullptr || view.buffer->data == nullptr ||
            view.offset + view.size > view.buffer->size || view.size == 0) {
            return ErrorCode::kCorruptData; // cgltf_validate covers this; belt and braces
        }
        const auto* encoded = static_cast<const stbi_uc*>(view.buffer->data) + view.offset;

        int width = 0;
        int height = 0;
        int components = 0;
        stbi_uc* pixels = stbi_load_from_memory(encoded, static_cast<int>(view.size), &width,
                                                &height, &components, 4);
        if (pixels == nullptr) {
            HUE_LOG_WARN("gltf: image decode failed: %s", stbi_failure_reason());
            return ErrorCode::kCorruptData;
        }
        struct PixelGuard {
            stbi_uc* pixels;
            ~PixelGuard() { stbi_image_free(pixels); }
        } pixel_guard{pixels};

        if (width <= 0 || height <= 0 || static_cast<std::uint32_t>(width) > kGltfMaxTextureDim ||
            static_cast<std::uint32_t>(height) > kGltfMaxTextureDim) {
            return ErrorCode::kCorruptData;
        }

        std::size_t decoded_bytes = 0;
        if (!checked_mul(static_cast<std::size_t>(width) * 4u, static_cast<std::size_t>(height),
                         decoded_bytes) ||
            !checked_add(decoded_total, decoded_bytes, decoded_total) ||
            decoded_total > kGltfMaxDecodedTextureBytes) {
            HUE_LOG_WARN("gltf: decoded texture bytes exceed cap");
            return ErrorCode::kCorruptData;
        }

        TextureData texture;
        texture.width = static_cast<std::uint32_t>(width);
        texture.height = static_cast<std::uint32_t>(height);
        texture.srgb = srgb_flags[t];
        if (!texture.pixels.append(pixels, decoded_bytes)) {
            return ErrorCode::kOutOfMemory;
        }
        if (!out.push_back(std::move(texture))) {
            return ErrorCode::kOutOfMemory;
        }
    }
    return {};
}

[[nodiscard]] Result<void> load_materials(const cgltf_data& data, Array<MaterialData>& out) {
    if (data.materials_count > kGltfMaxMaterials) {
        HUE_LOG_WARN("gltf: too many materials (%zu > %u)",
                     static_cast<std::size_t>(data.materials_count), kGltfMaxMaterials);
        return ErrorCode::kCorruptData;
    }

    for (cgltf_size m = 0; m < data.materials_count; ++m) {
        const cgltf_material& source = data.materials[m];
        MaterialData material;
        if (source.has_pbr_metallic_roughness) {
            const cgltf_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;
            material.base_color_factor = {clamped01(pbr.base_color_factor[0]),
                                          clamped01(pbr.base_color_factor[1]),
                                          clamped01(pbr.base_color_factor[2]),
                                          clamped01(pbr.base_color_factor[3])};
            material.metallic_factor = clamped01(pbr.metallic_factor);
            material.roughness_factor = clamped01(pbr.roughness_factor);
            material.base_color_texture = texture_index_of(data, pbr.base_color_texture.texture);
            material.metallic_roughness_texture =
                texture_index_of(data, pbr.metallic_roughness_texture.texture);
            const cgltf_size texture_count = data.textures_count;
            if (material.base_color_texture >= static_cast<std::int32_t>(texture_count) ||
                material.metallic_roughness_texture >= static_cast<std::int32_t>(texture_count)) {
                return ErrorCode::kCorruptData;
            }
        }
        if (!out.push_back(material)) {
            return ErrorCode::kOutOfMemory;
        }
    }
    return {};
}

[[nodiscard]] std::int32_t material_index_of(const cgltf_data& data,
                                             const cgltf_material* material) {
    if (material == nullptr) {
        return -1;
    }
    return static_cast<std::int32_t>(material - data.materials);
}

// ------------------------------------------------------------ geometry

struct GeometryTotals {
    std::uint32_t vertices = 0;
    std::uint32_t indices = 0;
};

// Validates and appends the indices of one primitive; every value is
// bounds-checked against this primitive's vertex count.
[[nodiscard]] Result<void> append_indices(Array<std::uint32_t>& out,
                                          const cgltf_primitive& primitive,
                                          cgltf_size vertex_count, cgltf_size& index_count) {
    index_count = vertex_count; // no index accessor: sequential triangles
    if (primitive.indices != nullptr) {
        if (primitive.indices->type != cgltf_type_scalar || primitive.indices->is_sparse) {
            return ErrorCode::kCorruptData;
        }
        index_count = primitive.indices->count;
    }
    if (index_count == 0 || index_count % 3 != 0) {
        return ErrorCode::kCorruptData;
    }
    if (!out.reserve(out.size() + index_count)) {
        return ErrorCode::kOutOfMemory;
    }
    for (cgltf_size i = 0; i < index_count; ++i) {
        cgltf_size index = i;
        if (primitive.indices != nullptr) {
            index = cgltf_accessor_read_index(primitive.indices, i);
        }
        if (index >= vertex_count) {
            HUE_LOG_WARN("gltf: index out of range (%zu >= %zu)",
                         static_cast<std::size_t>(index), static_cast<std::size_t>(vertex_count));
            return ErrorCode::kCorruptData;
        }
        if (!out.push_back(static_cast<std::uint32_t>(index))) {
            return ErrorCode::kOutOfMemory;
        }
    }
    return {};
}

// Finds POSITION / NORMAL / TEXCOORD_0 and validates their layout. Returns
// the vertex count through out-parameter; accessors may be null (optional).
[[nodiscard]] Result<cgltf_size> find_static_attributes(const cgltf_primitive& primitive,
                                                        const cgltf_accessor** positions,
                                                        const cgltf_accessor** normals,
                                                        const cgltf_accessor** uvs) {
    if (primitive.type != cgltf_primitive_type_triangles) {
        HUE_LOG_WARN("gltf: non-triangle primitive rejected");
        return ErrorCode::kUnsupported;
    }
    for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
        const cgltf_attribute& attribute = primitive.attributes[a];
        if (attribute.type == cgltf_attribute_type_position && attribute.index == 0) {
            *positions = attribute.data;
        } else if (attribute.type == cgltf_attribute_type_normal && attribute.index == 0) {
            *normals = attribute.data;
        } else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) {
            *uvs = attribute.data;
        }
    }
    if (*positions == nullptr || (*positions)->type != cgltf_type_vec3 ||
        (*positions)->component_type != cgltf_component_type_r_32f || (*positions)->is_sparse) {
        return ErrorCode::kCorruptData;
    }
    const cgltf_size vertex_count = (*positions)->count;
    if (vertex_count == 0) {
        return ErrorCode::kCorruptData;
    }
    if (*normals != nullptr && !accessor_is(*normals, cgltf_type_vec3, vertex_count)) {
        return ErrorCode::kCorruptData;
    }
    if (*uvs != nullptr && !accessor_is(*uvs, cgltf_type_vec2, vertex_count)) {
        return ErrorCode::kCorruptData;
    }
    return vertex_count;
}

[[nodiscard]] Result<void> append_primitive(StaticMeshData& out, const cgltf_data& data,
                                            const cgltf_primitive& primitive,
                                            GeometryTotals& totals) {
    const cgltf_accessor* positions = nullptr;
    const cgltf_accessor* normals = nullptr;
    const cgltf_accessor* uvs = nullptr;
    const auto found = find_static_attributes(primitive, &positions, &normals, &uvs);
    if (!found) {
        return found.error();
    }
    const cgltf_size vertex_count = found.value();
    if (vertex_count > kGltfMaxVertices - totals.vertices) {
        return ErrorCode::kCorruptData;
    }

    // ---- vertices
    const std::uint32_t vertex_offset = totals.vertices;
    if (!out.vertices.reserve(totals.vertices + vertex_count)) {
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

    // ---- indices
    const std::uint32_t first_index = totals.indices;
    cgltf_size index_count = 0;
    const auto indexed = append_indices(out.indices, primitive, vertex_count, index_count);
    if (!indexed) {
        return indexed.error();
    }
    if (index_count > kGltfMaxIndices - totals.indices) {
        return ErrorCode::kCorruptData;
    }

    MeshPrimitive mesh_primitive;
    mesh_primitive.index_count = static_cast<std::uint32_t>(index_count);
    mesh_primitive.first_index = first_index;
    mesh_primitive.vertex_offset = static_cast<std::int32_t>(vertex_offset);
    mesh_primitive.material_index = material_index_of(data, primitive.material);
    if (!out.primitives.push_back(mesh_primitive)) {
        return ErrorCode::kOutOfMemory;
    }

    totals.vertices += static_cast<std::uint32_t>(vertex_count);
    totals.indices += static_cast<std::uint32_t>(index_count);
    return {};
}

// Local-space AABB of one primitive, from the vertices just appended.
template <typename VertexArray>
[[nodiscard]] Aabb primitive_local_bounds(const VertexArray& vertices, std::uint32_t first,
                                          std::uint32_t vertex_count) {
    Aabb bounds{vertices[first].position, vertices[first].position};
    for (std::uint32_t v = 1; v < vertex_count; ++v) {
        bounds = bounds.expanded_to_include(vertices[first + v].position);
    }
    return bounds;
}

// ------------------------------------------------------------ file I/O

struct FileBytes {
    void* data = nullptr;
    std::size_t size = 0;
    ~FileBytes() {
        if (data != nullptr) {
            const auto freed = heap_free(data);
            (void)freed;
        }
    }
};

[[nodiscard]] Result<void> read_file_bytes(FileBytes& out, const char* path) {
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
    out.data = allocation.value();
    out.size = static_cast<std::size_t>(file_size);
    const std::size_t read = std::fread(out.data, 1, out.size, file);
    std::fclose(file);
    if (read != out.size) {
        return ErrorCode::kCorruptData;
    }
    return {};
}

} // namespace

// ---------------------------------------------------------------- static

Result<StaticMeshData> load_gltf(const void* bytes, std::size_t size) noexcept {
    DataGuard guard;
    const auto parsed = parse_and_validate(guard, bytes, size);
    if (!parsed) {
        return parsed.error();
    }
    const cgltf_data& data = *guard.data;

    StaticMeshData out;
    const auto textures = load_textures(data, out.textures);
    if (!textures) {
        return textures.error();
    }
    const auto materials = load_materials(data, out.materials);
    if (!materials) {
        return materials.error();
    }

    GeometryTotals totals;
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
            const std::uint32_t primitive_index =
                static_cast<std::uint32_t>(out.primitives.size());
            const std::uint32_t vertices_before = totals.vertices;
            const auto appended =
                append_primitive(out, data, node.mesh->primitives[p], totals);
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

            const Aabb local = primitive_local_bounds(out.vertices, vertices_before,
                                                      totals.vertices - vertices_before);
            const Aabb world_bounds = local.transformed(world);
            out.bounds = bounds_valid ? out.bounds.merged(world_bounds) : world_bounds;
            bounds_valid = true;
        }
    }

    if (total_primitives == 0) {
        return ErrorCode::kCorruptData; // nothing drawable is not a usable mesh asset
    }

    HUE_LOG_INFO("gltf: loaded %u primitives, %u instances, %u vertices, %u indices, "
                 "%zu textures, %zu materials",
                 total_primitives, total_instances, totals.vertices, totals.indices,
                 out.textures.size(), out.materials.size());
    return out;
}

Result<StaticMeshData> load_gltf_file(const char* path) noexcept {
    FileBytes file;
    const auto read = read_file_bytes(file, path);
    if (!read) {
        return read.error();
    }
    return load_gltf(file.data, file.size);
}

// --------------------------------------------------------------- skinned

namespace {

// Maps a node pointer to its index in the skin's joint list, -1 if the
// node is not a joint of this skin.
[[nodiscard]] std::int32_t joint_index_of(const cgltf_skin& skin, const cgltf_node* node) {
    if (node == nullptr) {
        return -1;
    }
    for (cgltf_size j = 0; j < skin.joints_count; ++j) {
        if (skin.joints[j] == node) {
            return static_cast<std::int32_t>(j);
        }
    }
    return -1;
}

// Orders joints so every parent precedes its children (single forward pass
// for pose propagation in Week 7). remap[old] = new. Fails on cycles.
[[nodiscard]] Result<void> topological_joint_order(const cgltf_skin& skin,
                                                   std::uint32_t* order,
                                                   std::uint32_t* remap) {
    const auto joint_count = static_cast<std::uint32_t>(skin.joints_count);
    std::int32_t parent_raw[kGltfMaxJoints];
    bool placed[kGltfMaxJoints] = {};
    for (std::uint32_t j = 0; j < joint_count; ++j) {
        parent_raw[j] = joint_index_of(skin, skin.joints[j]->parent);
    }

    std::uint32_t emitted = 0;
    while (emitted < joint_count) {
        const std::uint32_t before = emitted;
        for (std::uint32_t j = 0; j < joint_count; ++j) {
            if (placed[j]) {
                continue;
            }
            const std::int32_t parent = parent_raw[j];
            if (parent < 0 || placed[static_cast<std::uint32_t>(parent)]) {
                order[emitted] = j;
                remap[j] = emitted;
                placed[j] = true;
                ++emitted;
            }
        }
        if (emitted == before) {
            HUE_LOG_WARN("gltf: joint hierarchy has a cycle");
            return ErrorCode::kCorruptData;
        }
    }
    return {};
}

[[nodiscard]] Result<void> load_joints(const cgltf_skin& skin, const std::uint32_t* order,
                                       const std::uint32_t* remap, Array<Joint>& out) {
    const auto joint_count = static_cast<std::uint32_t>(skin.joints_count);
    if (skin.inverse_bind_matrices != nullptr &&
        !(skin.inverse_bind_matrices->type == cgltf_type_mat4 &&
          skin.inverse_bind_matrices->component_type == cgltf_component_type_r_32f &&
          !skin.inverse_bind_matrices->is_sparse &&
          skin.inverse_bind_matrices->count == joint_count)) {
        return ErrorCode::kCorruptData;
    }

    if (!out.reserve(joint_count)) {
        return ErrorCode::kOutOfMemory;
    }
    for (std::uint32_t j = 0; j < joint_count; ++j) {
        const std::uint32_t source_index = order[j];
        const cgltf_node& node = *skin.joints[source_index];

        Joint joint;
        if (node.has_matrix) {
            // Joint rest pose must be expressible as TRS for Week 7 blending.
            HUE_LOG_WARN("gltf: matrix-transform joint rejected");
            return ErrorCode::kUnsupported;
        }
        if (node.has_translation) {
            joint.rest_translation = {node.translation[0], node.translation[1],
                                      node.translation[2]};
        }
        if (node.has_rotation) {
            joint.rest_rotation = {node.rotation[0], node.rotation[1], node.rotation[2],
                                   node.rotation[3]};
        }
        if (node.has_scale) {
            joint.rest_scale = {node.scale[0], node.scale[1], node.scale[2]};
        }
        const float rest[10] = {joint.rest_translation.x, joint.rest_translation.y,
                                joint.rest_translation.z, joint.rest_rotation.x,
                                joint.rest_rotation.y,    joint.rest_rotation.z,
                                joint.rest_rotation.w,    joint.rest_scale.x,
                                joint.rest_scale.y,       joint.rest_scale.z};
        for (float component : rest) {
            if (!std::isfinite(component)) {
                return ErrorCode::kCorruptData;
            }
        }

        if (skin.inverse_bind_matrices != nullptr) {
            cgltf_float matrix[16] = {};
            if (cgltf_accessor_read_float(skin.inverse_bind_matrices, source_index, matrix,
                                          16) == 0) {
                return ErrorCode::kCorruptData;
            }
            for (float component : matrix) {
                if (!std::isfinite(component)) {
                    return ErrorCode::kCorruptData;
                }
            }
            std::memcpy(joint.inverse_bind.m, matrix, sizeof(matrix));
        }

        const std::int32_t parent_raw = joint_index_of(skin, node.parent);
        joint.parent = parent_raw < 0
                           ? -1
                           : static_cast<std::int32_t>(remap[static_cast<std::uint32_t>(
                                 parent_raw)]);
        if (node.name != nullptr) {
            std::strncpy(joint.name, node.name, sizeof(joint.name) - 1);
        }
        if (!out.push_back(joint)) {
            return ErrorCode::kOutOfMemory;
        }
    }
    return {};
}

[[nodiscard]] Result<void> append_skinned_primitive(SkinnedMeshData& out, const cgltf_data& data,
                                                    const cgltf_primitive& primitive,
                                                    const std::uint32_t* remap,
                                                    std::uint32_t joint_count,
                                                    GeometryTotals& totals) {
    const cgltf_accessor* positions = nullptr;
    const cgltf_accessor* normals = nullptr;
    const cgltf_accessor* uvs = nullptr;
    const auto found = find_static_attributes(primitive, &positions, &normals, &uvs);
    if (!found) {
        return found.error();
    }
    const cgltf_size vertex_count = found.value();
    if (vertex_count > kGltfMaxVertices - totals.vertices) {
        return ErrorCode::kCorruptData;
    }

    const cgltf_accessor* joints = nullptr;
    const cgltf_accessor* weights = nullptr;
    for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
        const cgltf_attribute& attribute = primitive.attributes[a];
        if (attribute.type == cgltf_attribute_type_joints && attribute.index == 0) {
            joints = attribute.data;
        } else if (attribute.type == cgltf_attribute_type_weights && attribute.index == 0) {
            weights = attribute.data;
        }
    }
    if (joints == nullptr || joints->type != cgltf_type_vec4 || joints->is_sparse ||
        joints->count != vertex_count ||
        (joints->component_type != cgltf_component_type_r_8u &&
         joints->component_type != cgltf_component_type_r_16u)) {
        return ErrorCode::kCorruptData;
    }
    if (!accessor_is(weights, cgltf_type_vec4, vertex_count)) {
        return ErrorCode::kCorruptData;
    }

    // ---- vertices
    const std::uint32_t vertex_offset = totals.vertices;
    if (!out.vertices.reserve(totals.vertices + vertex_count)) {
        return ErrorCode::kOutOfMemory;
    }
    for (cgltf_size v = 0; v < vertex_count; ++v) {
        SkinnedVertex vertex{};
        cgltf_float value[4] = {};
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

        // Joint indices: bounds-checked against the skin, then remapped to
        // the topological order the skeleton was stored in.
        cgltf_uint joint_indices[4] = {};
        if (cgltf_accessor_read_uint(joints, v, joint_indices, 4) == 0) {
            return ErrorCode::kCorruptData;
        }
        for (std::uint32_t c = 0; c < 4; ++c) {
            if (joint_indices[c] >= joint_count) {
                HUE_LOG_WARN("gltf: joint index out of range (%u >= %u)",
                             static_cast<std::uint32_t>(joint_indices[c]), joint_count);
                return ErrorCode::kCorruptData;
            }
            vertex.joints[c] = static_cast<std::uint16_t>(remap[joint_indices[c]]);
        }

        // Weights: finite, non-negative, renormalized to sum 1.
        if (cgltf_accessor_read_float(weights, v, value, 4) == 0) {
            return ErrorCode::kCorruptData;
        }
        float weight_sum = 0.0f;
        for (float weight : value) {
            if (!std::isfinite(weight) || weight < 0.0f) {
                return ErrorCode::kCorruptData;
            }
            weight_sum += weight;
        }
        if (weight_sum < 1.0e-6f) {
            return ErrorCode::kCorruptData; // unbound vertex would render at origin
        }
        for (std::uint32_t c = 0; c < 4; ++c) {
            vertex.weights[c] = value[c] / weight_sum;
        }

        if (!out.vertices.push_back(vertex)) {
            return ErrorCode::kOutOfMemory;
        }
    }

    // ---- indices
    const std::uint32_t first_index = totals.indices;
    cgltf_size index_count = 0;
    const auto indexed = append_indices(out.indices, primitive, vertex_count, index_count);
    if (!indexed) {
        return indexed.error();
    }
    if (index_count > kGltfMaxIndices - totals.indices) {
        return ErrorCode::kCorruptData;
    }

    MeshPrimitive mesh_primitive;
    mesh_primitive.index_count = static_cast<std::uint32_t>(index_count);
    mesh_primitive.first_index = first_index;
    mesh_primitive.vertex_offset = static_cast<std::int32_t>(vertex_offset);
    mesh_primitive.material_index = material_index_of(data, primitive.material);
    if (!out.primitives.push_back(mesh_primitive)) {
        return ErrorCode::kOutOfMemory;
    }

    totals.vertices += static_cast<std::uint32_t>(vertex_count);
    totals.indices += static_cast<std::uint32_t>(index_count);
    return {};
}

// Loads one animation into a clip. Channels that do not target a joint of
// this skin are skipped; a clip with zero joint channels loads as empty
// and the caller drops it.
[[nodiscard]] Result<void> load_clip(const cgltf_animation& animation, const cgltf_skin& skin,
                                     const std::uint32_t* remap, AnimationClip& out,
                                     std::size_t clip_number) {
    if (animation.name != nullptr) {
        std::strncpy(out.name, animation.name, sizeof(out.name) - 1);
    } else {
        std::snprintf(out.name, sizeof(out.name), "clip_%zu", clip_number);
    }

    if (animation.channels_count > kGltfMaxChannelsPerClip) {
        return ErrorCode::kCorruptData;
    }

    float duration = 0.0f;
    for (cgltf_size c = 0; c < animation.channels_count; ++c) {
        const cgltf_animation_channel& source = animation.channels[c];
        const std::int32_t joint_raw = joint_index_of(skin, source.target_node);
        if (joint_raw < 0) {
            continue; // channel animates a node outside this skin
        }

        AnimationPath path;
        std::uint32_t components = 0;
        cgltf_type output_type;
        switch (source.target_path) {
        case cgltf_animation_path_type_translation:
            path = AnimationPath::kTranslation;
            components = 3;
            output_type = cgltf_type_vec3;
            break;
        case cgltf_animation_path_type_rotation:
            path = AnimationPath::kRotation;
            components = 4;
            output_type = cgltf_type_vec4;
            break;
        case cgltf_animation_path_type_scale:
            path = AnimationPath::kScale;
            components = 3;
            output_type = cgltf_type_vec3;
            break;
        default:
            HUE_LOG_WARN("gltf: unsupported animation path rejected");
            return ErrorCode::kUnsupported; // morph weights
        }

        const cgltf_animation_sampler* sampler = source.sampler;
        if (sampler == nullptr) {
            return ErrorCode::kCorruptData;
        }
        if (sampler->interpolation != cgltf_interpolation_type_linear) {
            HUE_LOG_WARN("gltf: non-LINEAR animation interpolation rejected");
            return ErrorCode::kUnsupported;
        }
        const cgltf_accessor* input = sampler->input;
        const cgltf_accessor* output = sampler->output;
        if (input == nullptr || input->type != cgltf_type_scalar ||
            input->component_type != cgltf_component_type_r_32f || input->is_sparse ||
            input->count == 0 || input->count > kGltfMaxKeyframes) {
            return ErrorCode::kCorruptData;
        }
        if (output == nullptr || output->type != output_type ||
            output->component_type != cgltf_component_type_r_32f || output->is_sparse ||
            output->count != input->count) {
            return ErrorCode::kCorruptData;
        }

        AnimationChannel channel;
        channel.joint = remap[static_cast<std::uint32_t>(joint_raw)];
        channel.path = path;

        // Times: finite, non-negative, non-decreasing, capped duration.
        if (!channel.times.reserve(input->count)) {
            return ErrorCode::kOutOfMemory;
        }
        float previous = 0.0f;
        for (cgltf_size k = 0; k < input->count; ++k) {
            cgltf_float time = 0.0f;
            if (cgltf_accessor_read_float(input, k, &time, 1) == 0) {
                return ErrorCode::kCorruptData;
            }
            if (!std::isfinite(time) || time < 0.0f || time < previous ||
                time > kGltfMaxClipSeconds) {
                return ErrorCode::kCorruptData;
            }
            previous = time;
            if (!channel.times.push_back(time)) {
                return ErrorCode::kOutOfMemory;
            }
        }
        duration = previous > duration ? previous : duration;

        // Values: finite; rotations must not be zero-length (they get
        // normalized when sampled, so a zero quat would divide by zero).
        if (!channel.values.reserve(input->count * components)) {
            return ErrorCode::kOutOfMemory;
        }
        for (cgltf_size k = 0; k < input->count; ++k) {
            cgltf_float value[4] = {};
            if (cgltf_accessor_read_float(output, k, value, components) == 0) {
                return ErrorCode::kCorruptData;
            }
            float length_squared = 0.0f;
            for (std::uint32_t component = 0; component < components; ++component) {
                if (!std::isfinite(value[component])) {
                    return ErrorCode::kCorruptData;
                }
                length_squared += value[component] * value[component];
                if (!channel.values.push_back(value[component])) {
                    return ErrorCode::kOutOfMemory;
                }
            }
            if (path == AnimationPath::kRotation && length_squared < 1.0e-8f) {
                return ErrorCode::kCorruptData;
            }
        }

        if (!out.channels.push_back(std::move(channel))) {
            return ErrorCode::kOutOfMemory;
        }
    }

    out.duration = duration;
    return {};
}

} // namespace

Result<SkinnedMeshData> load_gltf_skinned(const void* bytes, std::size_t size) noexcept {
    DataGuard guard;
    const auto parsed = parse_and_validate(guard, bytes, size);
    if (!parsed) {
        return parsed.error();
    }
    const cgltf_data& data = *guard.data;

    // First node carrying both a mesh and a skin picks the skeleton.
    // KayKit-style characters split the body across several mesh nodes
    // that share that skin; collect every primitive on that skin.
    const cgltf_node* skinned_node = nullptr;
    for (cgltf_size n = 0; n < data.nodes_count; ++n) {
        if (data.nodes[n].mesh != nullptr && data.nodes[n].skin != nullptr) {
            skinned_node = &data.nodes[n];
            break;
        }
    }
    if (skinned_node == nullptr) {
        HUE_LOG_WARN("gltf: no skinned mesh in file");
        return ErrorCode::kCorruptData;
    }
    const cgltf_skin& skin = *skinned_node->skin;
    if (skin.joints_count == 0 || skin.joints_count > kGltfMaxJoints) {
        return ErrorCode::kCorruptData;
    }
    const auto joint_count = static_cast<std::uint32_t>(skin.joints_count);

    SkinnedMeshData out;
    const auto textures = load_textures(data, out.textures);
    if (!textures) {
        return textures.error();
    }
    const auto materials = load_materials(data, out.materials);
    if (!materials) {
        return materials.error();
    }

    std::uint32_t order[kGltfMaxJoints] = {};
    std::uint32_t remap[kGltfMaxJoints] = {};
    const auto ordered = topological_joint_order(skin, order, remap);
    if (!ordered) {
        return ordered.error();
    }
    const auto joints = load_joints(skin, order, remap, out.joints);
    if (!joints) {
        return joints.error();
    }

    // ---- geometry (bind pose): every mesh node that uses this skin.
    GeometryTotals totals;
    bool have_primitive = false;
    for (cgltf_size n = 0; n < data.nodes_count; ++n) {
        const cgltf_node& node = data.nodes[n];
        if (node.mesh == nullptr || node.skin != &skin) {
            continue;
        }
        const cgltf_mesh& mesh = *node.mesh;
        if (mesh.primitives_count == 0 || mesh.primitives_count > kGltfMaxPrimitives) {
            return ErrorCode::kCorruptData;
        }
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const std::uint32_t vertices_before = totals.vertices;
            const auto appended = append_skinned_primitive(out, data, mesh.primitives[p], remap,
                                                           joint_count, totals);
            if (!appended) {
                return appended.error();
            }
            const Aabb local = primitive_local_bounds(out.vertices, vertices_before,
                                                      totals.vertices - vertices_before);
            out.bounds = have_primitive ? out.bounds.merged(local) : local;
            have_primitive = true;
        }
    }
    if (!have_primitive) {
        return ErrorCode::kCorruptData;
    }

    // ---- animation clips
    if (data.animations_count > kGltfMaxClips) {
        return ErrorCode::kCorruptData;
    }
    for (cgltf_size a = 0; a < data.animations_count; ++a) {
        AnimationClip clip;
        const auto loaded = load_clip(data.animations[a], skin, remap, clip, a);
        if (!loaded) {
            return loaded.error();
        }
        if (clip.channels.size() == 0) {
            continue; // animation targets nothing on this skeleton
        }
        if (!out.clips.push_back(std::move(clip))) {
            return ErrorCode::kOutOfMemory;
        }
    }

    HUE_LOG_INFO("gltf: skinned mesh loaded: %zu vertices, %zu indices, %u joints, %zu clips, "
                 "%zu textures, %zu materials",
                 out.vertices.size(), out.indices.size(), joint_count, out.clips.size(),
                 out.textures.size(), out.materials.size());
    return out;
}

Result<SkinnedMeshData> load_gltf_skinned_file(const char* path) noexcept {
    FileBytes file;
    const auto read = read_file_bytes(file, path);
    if (!read) {
        return read.error();
    }
    return load_gltf_skinned(file.data, file.size);
}

} // namespace hue::asset
