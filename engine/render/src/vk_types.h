// engine/render/src/vk_types.h
//
// Internal shared state for the render module. Never included from public
// headers. C-style module split: context / swapchain / depth / pipeline /
// buffer / mesh each own a state struct plus create/destroy functions;
// renderer.cpp composes them.

#pragma once

#include <volk.h>

#include <vk_mem_alloc.h>

#include "hue/asset/mesh_data.h"
#include "hue/core/log.h"
#include "hue/core/result.h"
#include "hue/render/mesh.h"

struct GLFWwindow;

namespace hue::render {

// Logs the failing expression and VkResult, then returns kUnknown from the
// enclosing Result-returning function.
#define HUE_VK_TRY(expr)                                                                           \
    do {                                                                                           \
        const VkResult hue_vk_result = (expr);                                                     \
        if (hue_vk_result != VK_SUCCESS) {                                                         \
            HUE_LOG_ERROR("vulkan: %s failed (VkResult %d)", #expr,                                \
                          static_cast<int>(hue_vk_result));                                        \
            return ::hue::ErrorCode::kUnknown;                                                     \
        }                                                                                          \
    } while (false)

inline constexpr std::uint32_t kFramesInFlight = 2;
inline constexpr std::uint32_t kMaxSwapchainImages = 8;
inline constexpr std::uint32_t kMaxMeshes = 256;

struct ContextState {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE; // graphics + present, same family
    std::uint32_t queue_family = 0;
    VmaAllocator allocator = VK_NULL_HANDLE;
    char adapter_name[256] = {};
};

struct SwapchainState {
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {0, 0};
    std::uint32_t image_count = 0;
    VkImage images[kMaxSwapchainImages] = {};
    VkImageView views[kMaxSwapchainImages] = {};
};

struct DepthState {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_D32_SFLOAT;
    VkExtent2D extent = {0, 0};
};

struct PipelineState {
    // Triangle (Week 4 bring-up, still drawn when the frame has no meshes).
    VkPipelineLayout triangle_layout = VK_NULL_HANDLE;
    VkPipeline triangle = VK_NULL_HANDLE;
    // Static meshes: vertex input + depth test + 128B push constants.
    VkPipelineLayout mesh_layout = VK_NULL_HANDLE;
    VkPipeline mesh = VK_NULL_HANDLE;
};

struct BufferAllocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

// One GPU-resident static mesh: device-local vertex/index buffers plus the
// CPU-side primitive/instance tables needed to record draws.
struct GpuMesh {
    BufferAllocation vertices;
    BufferAllocation indices;
    Array<asset::MeshPrimitive> primitives{MemoryTag::kRender};
    Array<asset::MeshInstance> instances{MemoryTag::kRender};
    Aabb bounds;
    std::uint32_t generation = 0;
    bool used = false;
};

struct MeshRegistry {
    GpuMesh slots[kMaxMeshes];
};

// Pushed per drawn primitive instance; must stay within the 128-byte
// push-constant floor the spec guarantees.
struct MeshPushConstants {
    Mat4 mvp;
    Mat4 model;
};
static_assert(sizeof(MeshPushConstants) == 128);

// context.cpp
Result<void> context_create(ContextState& context, GLFWwindow* window, bool enable_validation);
void context_destroy(ContextState& context);

// swapchain.cpp
Result<void> swapchain_create(SwapchainState& swapchain, const ContextState& context,
                              GLFWwindow* window);
void swapchain_destroy(SwapchainState& swapchain, const ContextState& context);

// depth.cpp
Result<void> depth_create(DepthState& depth, const ContextState& context, VkExtent2D extent);
void depth_destroy(DepthState& depth, const ContextState& context);

// pipeline.cpp
Result<void> pipeline_create(PipelineState& pipeline, const ContextState& context,
                             VkFormat color_format, VkFormat depth_format,
                             const char* shader_directory);
void pipeline_destroy(PipelineState& pipeline, const ContextState& context);

// buffer.cpp: creates a device-local buffer and fills it through a staging
// buffer + one-time submit (waits for the copy; upload is a load-time path).
Result<BufferAllocation> device_buffer_create(const ContextState& context, const void* data,
                                              std::size_t size, VkBufferUsageFlags usage);
void buffer_destroy(BufferAllocation& buffer, const ContextState& context);

// mesh.cpp
Result<MeshHandle> mesh_registry_upload(MeshRegistry& registry, const ContextState& context,
                                        const asset::StaticMeshData& data);
const GpuMesh* mesh_registry_resolve(const MeshRegistry& registry, MeshHandle handle);
void mesh_registry_destroy(MeshRegistry& registry, const ContextState& context);

} // namespace hue::render
