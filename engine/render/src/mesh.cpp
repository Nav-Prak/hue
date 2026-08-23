// engine/render/src/mesh.cpp
//
// GPU mesh registry: fixed slot table with generation counters. Upload
// copies validated StaticMeshData into device-local buffers, uploads the
// mesh's textures with full mip chains, and allocates one descriptor set
// per material (untextured slots fall back to the renderer's 1x1 whites).

#include "vk_types.h"

namespace hue::render {

Result<MeshHandle> mesh_registry_upload(MeshRegistry& registry, const ContextState& context,
                                        DescriptorState& descriptors, SamplerCache& samplers,
                                        VkImageView fallback_base_color,
                                        VkImageView fallback_metallic_roughness,
                                        const asset::StaticMeshData& data) {
    if (data.vertices.size() == 0 || data.indices.size() == 0 || data.primitives.size() == 0) {
        return ErrorCode::kInvalidArgument;
    }
    // Loader-validated data keeps these invariants; procedural callers must
    // uphold them too, so re-check at the upload boundary.
    for (std::size_t p = 0; p < data.primitives.size(); ++p) {
        const std::int32_t material = data.primitives[p].material_index;
        if (material >= static_cast<std::int32_t>(data.materials.size())) {
            return ErrorCode::kInvalidArgument;
        }
    }
    for (std::size_t m = 0; m < data.materials.size(); ++m) {
        if (data.materials[m].base_color_texture >=
                static_cast<std::int32_t>(data.textures.size()) ||
            data.materials[m].metallic_roughness_texture >=
                static_cast<std::int32_t>(data.textures.size())) {
            return ErrorCode::kInvalidArgument;
        }
    }

    std::uint32_t slot = UINT32_MAX;
    for (std::uint32_t i = 0; i < kMaxMeshes; ++i) {
        if (!registry.slots[i].used) {
            slot = i;
            break;
        }
    }
    if (slot == UINT32_MAX) {
        HUE_LOG_ERROR("mesh registry full (%u slots)", kMaxMeshes);
        return ErrorCode::kOutOfMemory;
    }
    GpuMesh& mesh = registry.slots[slot];

    auto release = [&]() {
        buffer_destroy(mesh.vertices, context);
        buffer_destroy(mesh.indices, context);
        for (std::size_t t = 0; t < mesh.textures.size(); ++t) {
            texture_destroy(mesh.textures[t], context);
        }
        mesh.textures.clear();
        mesh.materials.clear();
        mesh.primitives.clear();
        mesh.instances.clear();
    };

    // ---- geometry
    auto vertices = device_buffer_create(context, data.vertices.data(),
                                         data.vertices.size() * sizeof(asset::StaticVertex),
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    if (!vertices) {
        return vertices.error();
    }
    mesh.vertices = vertices.value();

    auto indices = device_buffer_create(context, data.indices.data(),
                                        data.indices.size() * sizeof(std::uint32_t),
                                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    if (!indices) {
        release();
        return indices.error();
    }
    mesh.indices = indices.value();

    // ---- textures (full mip chains)
    for (std::size_t t = 0; t < data.textures.size(); ++t) {
        auto texture = texture_upload(context, data.textures[t]);
        if (!texture) {
            release();
            return texture.error();
        }
        if (!mesh.textures.push_back(std::move(texture.value()))) {
            release();
            return ErrorCode::kOutOfMemory;
        }
    }

    // ---- materials (one descriptor set each)
    auto sampler = sampler_cache_get(samplers, context, VK_FILTER_LINEAR,
                                     VK_SAMPLER_ADDRESS_MODE_REPEAT);
    if (!sampler) {
        release();
        return sampler.error();
    }
    for (std::size_t m = 0; m < data.materials.size(); ++m) {
        const asset::MaterialData& source = data.materials[m];
        GpuMaterial material;
        material.base_color_factor = source.base_color_factor;
        material.metallic_factor = source.metallic_factor;
        material.roughness_factor = source.roughness_factor;

        const VkImageView base_color =
            source.base_color_texture >= 0
                ? mesh.textures[static_cast<std::size_t>(source.base_color_texture)].view
                : fallback_base_color;
        const VkImageView metallic_roughness =
            source.metallic_roughness_texture >= 0
                ? mesh.textures[static_cast<std::size_t>(source.metallic_roughness_texture)].view
                : fallback_metallic_roughness;

        auto set = material_set_allocate(descriptors, context, base_color, metallic_roughness,
                                         sampler.value());
        if (!set) {
            release();
            return set.error();
        }
        material.set = set.value();
        if (!mesh.materials.push_back(material)) {
            release();
            return ErrorCode::kOutOfMemory;
        }
    }

    // ---- CPU-side draw tables
    for (std::size_t p = 0; p < data.primitives.size(); ++p) {
        if (!mesh.primitives.push_back(data.primitives[p])) {
            release();
            return ErrorCode::kOutOfMemory;
        }
    }
    for (std::size_t i = 0; i < data.instances.size(); ++i) {
        if (!mesh.instances.push_back(data.instances[i])) {
            release();
            return ErrorCode::kOutOfMemory;
        }
    }

    mesh.bounds = data.bounds;
    mesh.skinned = false;
    mesh.joint_count = 0;
    mesh.used = true;
    ++mesh.generation;

    MeshHandle handle;
    handle.slot = slot;
    handle.generation = mesh.generation;
    HUE_LOG_INFO("mesh uploaded: slot %u, %zu vertices, %zu indices, %zu primitives, "
                 "%zu textures, %zu materials",
                 slot, data.vertices.size(), data.indices.size(), data.primitives.size(),
                 data.textures.size(), data.materials.size());
    return handle;
}

Result<MeshHandle> mesh_registry_upload_skinned(
    MeshRegistry& registry, const ContextState& context, DescriptorState& descriptors,
    SamplerCache& samplers, VkImageView fallback_base_color,
    VkImageView fallback_metallic_roughness, const asset::SkinnedMeshData& data) {
    if (data.vertices.empty() || data.indices.empty() || data.primitives.empty() ||
        data.joints.empty() || data.joints.size() > asset::kGltfMaxJoints) {
        return ErrorCode::kInvalidArgument;
    }
    for (std::size_t p = 0; p < data.primitives.size(); ++p) {
        const std::int32_t material = data.primitives[p].material_index;
        if (material >= static_cast<std::int32_t>(data.materials.size())) {
            return ErrorCode::kInvalidArgument;
        }
    }
    for (std::size_t v = 0; v < data.vertices.size(); ++v) {
        for (std::uint32_t influence = 0; influence < 4; ++influence) {
            if (data.vertices[v].joints[influence] >= data.joints.size()) {
                return ErrorCode::kInvalidArgument;
            }
        }
    }
    for (std::size_t m = 0; m < data.materials.size(); ++m) {
        if (data.materials[m].base_color_texture >=
                static_cast<std::int32_t>(data.textures.size()) ||
            data.materials[m].metallic_roughness_texture >=
                static_cast<std::int32_t>(data.textures.size())) {
            return ErrorCode::kInvalidArgument;
        }
    }

    std::uint32_t slot = UINT32_MAX;
    for (std::uint32_t i = 0; i < kMaxMeshes; ++i) {
        if (!registry.slots[i].used) {
            slot = i;
            break;
        }
    }
    if (slot == UINT32_MAX) return ErrorCode::kOutOfMemory;
    GpuMesh& mesh = registry.slots[slot];
    auto release = [&]() {
        buffer_destroy(mesh.vertices, context);
        buffer_destroy(mesh.indices, context);
        for (std::size_t t = 0; t < mesh.textures.size(); ++t) {
            texture_destroy(mesh.textures[t], context);
        }
        mesh.textures.clear();
        mesh.materials.clear();
        mesh.primitives.clear();
        mesh.instances.clear();
    };

    auto vertices = device_buffer_create(context, data.vertices.data(),
                                         data.vertices.size() * sizeof(asset::SkinnedVertex),
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    if (!vertices) return vertices.error();
    mesh.vertices = vertices.value();
    auto indices = device_buffer_create(context, data.indices.data(),
                                        data.indices.size() * sizeof(std::uint32_t),
                                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    if (!indices) {
        release();
        return indices.error();
    }
    mesh.indices = indices.value();

    for (std::size_t t = 0; t < data.textures.size(); ++t) {
        auto texture = texture_upload(context, data.textures[t]);
        if (!texture) {
            release();
            return texture.error();
        }
        if (!mesh.textures.push_back(std::move(texture.value()))) {
            release();
            return ErrorCode::kOutOfMemory;
        }
    }
    auto sampler = sampler_cache_get(samplers, context, VK_FILTER_LINEAR,
                                     VK_SAMPLER_ADDRESS_MODE_REPEAT);
    if (!sampler) {
        release();
        return sampler.error();
    }
    for (std::size_t m = 0; m < data.materials.size(); ++m) {
        const asset::MaterialData& source = data.materials[m];
        GpuMaterial material;
        material.base_color_factor = source.base_color_factor;
        material.metallic_factor = source.metallic_factor;
        material.roughness_factor = source.roughness_factor;
        const VkImageView base_color =
            source.base_color_texture >= 0
                ? mesh.textures[static_cast<std::size_t>(source.base_color_texture)].view
                : fallback_base_color;
        const VkImageView metallic_roughness =
            source.metallic_roughness_texture >= 0
                ? mesh.textures[static_cast<std::size_t>(source.metallic_roughness_texture)].view
                : fallback_metallic_roughness;
        auto set = material_set_allocate(descriptors, context, base_color, metallic_roughness,
                                         sampler.value());
        if (!set) {
            release();
            return set.error();
        }
        material.set = set.value();
        if (!mesh.materials.push_back(material)) {
            release();
            return ErrorCode::kOutOfMemory;
        }
    }

    for (std::size_t p = 0; p < data.primitives.size(); ++p) {
        if (!mesh.primitives.push_back(data.primitives[p])) {
            release();
            return ErrorCode::kOutOfMemory;
        }
        asset::MeshInstance instance;
        instance.primitive_index = static_cast<std::uint32_t>(p);
        if (!mesh.instances.push_back(instance)) {
            release();
            return ErrorCode::kOutOfMemory;
        }
    }
    mesh.bounds = data.bounds;
    mesh.skinned = true;
    mesh.joint_count = static_cast<std::uint32_t>(data.joints.size());
    mesh.used = true;
    ++mesh.generation;
    MeshHandle handle{slot, mesh.generation};
    HUE_LOG_INFO("skinned mesh uploaded: slot %u, %zu vertices, %zu indices, %u joints", slot,
                 data.vertices.size(), data.indices.size(), mesh.joint_count);
    return handle;
}

const GpuMesh* mesh_registry_resolve(const MeshRegistry& registry, MeshHandle handle) {
    if (handle.slot >= kMaxMeshes) {
        return nullptr;
    }
    const GpuMesh& mesh = registry.slots[handle.slot];
    if (!mesh.used || mesh.generation != handle.generation) {
        return nullptr; // stale or destroyed handle
    }
    return &mesh;
}

void mesh_registry_destroy(MeshRegistry& registry, const ContextState& context) {
    for (std::uint32_t i = 0; i < kMaxMeshes; ++i) {
        GpuMesh& mesh = registry.slots[i];
        if (mesh.used) {
            buffer_destroy(mesh.vertices, context);
            buffer_destroy(mesh.indices, context);
            for (std::size_t t = 0; t < mesh.textures.size(); ++t) {
                texture_destroy(mesh.textures[t], context);
            }
            mesh.textures.clear();
            mesh.materials.clear();
            mesh.primitives.clear();
            mesh.instances.clear();
            mesh.used = false;
        }
    }
}

} // namespace hue::render
