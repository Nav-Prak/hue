// engine/render/src/mesh.cpp
//
// GPU mesh registry: fixed slot table with generation counters. Upload
// copies validated StaticMeshData into device-local buffers and keeps the
// CPU-side primitive/instance tables for draw recording.

#include "vk_types.h"

namespace hue::render {

Result<MeshHandle> mesh_registry_upload(MeshRegistry& registry, const ContextState& context,
                                        const asset::StaticMeshData& data) {
    if (data.vertices.size() == 0 || data.indices.size() == 0 || data.primitives.size() == 0) {
        return ErrorCode::kInvalidArgument;
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

    auto vertices = device_buffer_create(context, data.vertices.data(),
                                         data.vertices.size() * sizeof(asset::StaticVertex),
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    if (!vertices) {
        return vertices.error();
    }
    auto indices = device_buffer_create(context, data.indices.data(),
                                        data.indices.size() * sizeof(std::uint32_t),
                                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    if (!indices) {
        BufferAllocation vertex_buffer = vertices.value();
        buffer_destroy(vertex_buffer, context);
        return indices.error();
    }

    mesh.vertices = vertices.value();
    mesh.indices = indices.value();

    auto release = [&]() {
        buffer_destroy(mesh.vertices, context);
        buffer_destroy(mesh.indices, context);
        mesh.primitives.clear();
        mesh.instances.clear();
    };

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
    mesh.used = true;
    ++mesh.generation;

    MeshHandle handle;
    handle.slot = slot;
    handle.generation = mesh.generation;
    HUE_LOG_INFO("mesh uploaded: slot %u, %zu vertices, %zu indices, %zu primitives", slot,
                 data.vertices.size(), data.indices.size(), data.primitives.size());
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
            mesh.primitives.clear();
            mesh.instances.clear();
            mesh.used = false;
        }
    }
}

} // namespace hue::render
