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
#include "hue/asset/gltf_loader.h"
#include "hue/core/log.h"
#include "hue/core/result.h"
#include "hue/render/mesh.h"
#include "hue/render/renderer.h"

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
inline constexpr std::uint32_t kMaxMaterialSets = 1024;
inline constexpr std::uint32_t kMaxSamplers = 8;
inline constexpr std::uint32_t kMaxSkinnedDraws = 64;

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
    VkPipeline skinned_mesh = VK_NULL_HANDLE;
};

struct BufferAllocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

// One GPU-resident texture: device-local image with a full mip chain
// (generated at upload via blits), sampled-only after upload.
struct GpuTexture {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    std::uint32_t mip_levels = 1;
};

// Small fixed sampler cache: samplers are keyed by (filter, address mode)
// and created on first request. Week 6 content uses one or two.
struct SamplerCache {
    struct Entry {
        VkFilter filter = VK_FILTER_LINEAR;
        VkSamplerAddressMode address = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSampler sampler = VK_NULL_HANDLE;
    };
    Entry entries[kMaxSamplers];
    std::uint32_t count = 0;
};

// Per-frame uniforms (std140). Camera + one directional key light + up to
// kMaxPointLights local lights (Week 6 spec); the lighting pass grows
// shadows and mood in Week 16.
struct FrameUniforms {
    Mat4 view_projection;
    Vec4 camera_position;       // xyz, w unused
    Vec4 light_direction;       // xyz normalized world direction, w = point light count
    Vec4 light_color;           // rgb color, a intensity
    Vec4 ambient_color;         // rgb, a unused
    Vec4 point_position_radius[kMaxPointLights]; // xyz position, w radius
    Vec4 point_color_intensity[kMaxPointLights]; // rgb color, a intensity
};

// Descriptor plumbing: set 0 = frame UBO (one per frame in flight),
// set 1 = material textures (allocated per material at mesh upload; sets
// live until the pool dies at shutdown, meshes are never destroyed alone).
struct DescriptorState {
    VkDescriptorSetLayout frame_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout material_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout skin_layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    BufferAllocation frame_ubos[kFramesInFlight];
    void* frame_ubo_mapped[kFramesInFlight] = {};
    VkDescriptorSet frame_sets[kFramesInFlight] = {};
    BufferAllocation skin_buffers[kFramesInFlight];
    void* skin_buffer_mapped[kFramesInFlight] = {};
    VkDescriptorSet skin_sets[kFramesInFlight] = {};
};

// GPU-side material: factors pushed per draw, textures bound via set 1.
struct GpuMaterial {
    Vec4 base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic_factor = 1.0f;
    float roughness_factor = 1.0f;
    VkDescriptorSet set = VK_NULL_HANDLE;
};

// One GPU-resident static mesh: device-local vertex/index buffers plus the
// CPU-side primitive/instance/material tables needed to record draws.
struct GpuMesh {
    BufferAllocation vertices;
    BufferAllocation indices;
    Array<asset::MeshPrimitive> primitives{MemoryTag::kRender};
    Array<asset::MeshInstance> instances{MemoryTag::kRender};
    Array<GpuTexture> textures{MemoryTag::kRender};
    Array<GpuMaterial> materials{MemoryTag::kRender};
    Aabb bounds;
    std::uint32_t joint_count = 0;
    std::uint32_t generation = 0;
    bool skinned = false;
    bool used = false;
};

struct MeshRegistry {
    GpuMesh slots[kMaxMeshes];
};

// Pushed per drawn primitive instance; well under the 128-byte floor. The
// view-projection matrix moved to the frame UBO in Week 6 to make room for
// material factors.
struct MeshPushConstants {
    Mat4 model;              // 64B
    Vec4 base_color;         // 16B
    Vec4 metallic_roughness; // x = metallic, y = roughness, zw unused
    std::uint32_t skin_offset = 0; // index of the draw's first joint matrix
    std::uint32_t padding[3] = {};
};
static_assert(sizeof(MeshPushConstants) == 112);

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

// pipeline.cpp (set layouts are inputs: descriptors.cpp owns them)
Result<void> pipeline_create(PipelineState& pipeline, const ContextState& context,
                             VkFormat color_format, VkFormat depth_format,
                             const char* shader_directory,
                             const DescriptorState& descriptors);
void pipeline_destroy(PipelineState& pipeline, const ContextState& context);

// buffer.cpp: creates a device-local buffer and fills it through a staging
// buffer + one-time submit (waits for the copy; upload is a load-time path).
Result<BufferAllocation> device_buffer_create(const ContextState& context, const void* data,
                                              std::size_t size, VkBufferUsageFlags usage);
void buffer_destroy(BufferAllocation& buffer, const ContextState& context);

// texture.cpp: staging upload + mip chain generation (blit ladder), sRGB or
// UNORM per the asset flag. Load-time path, synchronous like buffers.
Result<GpuTexture> texture_upload(const ContextState& context, const asset::TextureData& data);
void texture_destroy(GpuTexture& texture, const ContextState& context);
Result<VkSampler> sampler_cache_get(SamplerCache& cache, const ContextState& context,
                                    VkFilter filter, VkSamplerAddressMode address);
void sampler_cache_destroy(SamplerCache& cache, const ContextState& context);

// descriptors.cpp
Result<void> descriptors_create(DescriptorState& descriptors, const ContextState& context);
void descriptors_destroy(DescriptorState& descriptors, const ContextState& context);
Result<VkDescriptorSet> material_set_allocate(DescriptorState& descriptors,
                                              const ContextState& context, VkImageView base_color,
                                              VkImageView metallic_roughness, VkSampler sampler);

// mesh.cpp (uploads geometry + textures + materials; fallback views back
// untextured material slots)
Result<MeshHandle> mesh_registry_upload(MeshRegistry& registry, const ContextState& context,
                                        DescriptorState& descriptors, SamplerCache& samplers,
                                        VkImageView fallback_base_color,
                                        VkImageView fallback_metallic_roughness,
                                        const asset::StaticMeshData& data);
Result<MeshHandle> mesh_registry_upload_skinned(
    MeshRegistry& registry, const ContextState& context, DescriptorState& descriptors,
    SamplerCache& samplers, VkImageView fallback_base_color,
    VkImageView fallback_metallic_roughness, const asset::SkinnedMeshData& data);
const GpuMesh* mesh_registry_resolve(const MeshRegistry& registry, MeshHandle handle);
void mesh_registry_destroy(MeshRegistry& registry, const ContextState& context);

} // namespace hue::render
