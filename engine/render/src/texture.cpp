// engine/render/src/texture.cpp
//
// Texture upload (Week 6 spec): staging buffer -> mip 0, then a blit
// ladder generates the full chain, then everything transitions to
// SHADER_READ_ONLY. Synchronous like buffer uploads; streaming comes
// later. R8G8B8A8 SRGB/UNORM both carry mandatory blit + linear-filter
// support, so no per-format capability probing is needed here.
//
// Also home of the sampler cache: samplers are immutable and few, so a
// fixed table keyed by (filter, address mode) covers the engine.

#include "vk_types.h"

#include <cstring>

namespace hue::render {

namespace {

[[nodiscard]] std::uint32_t mip_level_count(std::uint32_t width, std::uint32_t height) {
    std::uint32_t largest = width > height ? width : height;
    std::uint32_t levels = 1;
    while (largest > 1) {
        largest /= 2;
        ++levels;
    }
    return levels;
}

// One-time submit scope for the upload commands (same pattern as
// buffer.cpp; kept local because the two paths record different commands).
struct UploadGuard {
    const ContextState* context = nullptr;
    BufferAllocation staging;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    ~UploadGuard() {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(context->device, fence, nullptr);
        }
        if (pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(context->device, pool, nullptr);
        }
        if (staging.buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(context->allocator, staging.buffer, staging.allocation);
        }
    }
};

void image_barrier(VkCommandBuffer cmd, VkImage image, std::uint32_t base_mip,
                   std::uint32_t mip_count, VkImageLayout old_layout, VkImageLayout new_layout,
                   VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                   VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = src_stage;
    barrier.srcAccessMask = src_access;
    barrier.dstStageMask = dst_stage;
    barrier.dstAccessMask = dst_access;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = base_mip;
    barrier.subresourceRange.levelCount = mip_count;
    barrier.subresourceRange.layerCount = 1;

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

} // namespace

Result<GpuTexture> texture_upload(const ContextState& context, const asset::TextureData& data) {
    const std::size_t expected_bytes =
        static_cast<std::size_t>(data.width) * static_cast<std::size_t>(data.height) * 4u;
    if (data.width == 0 || data.height == 0 || data.pixels.size() != expected_bytes) {
        return ErrorCode::kInvalidArgument;
    }

    GpuTexture texture;
    texture.mip_levels = mip_level_count(data.width, data.height);
    const VkFormat format = data.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    // ---- device-local image with the full mip chain
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {data.width, data.height, 1};
    image_info.mipLevels = texture.mip_levels;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo image_alloc{};
    image_alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    HUE_VK_TRY(vmaCreateImage(context.allocator, &image_info, &image_alloc, &texture.image,
                              &texture.allocation, nullptr));

    auto fail = [&](ErrorCode code) -> Result<GpuTexture> {
        texture_destroy(texture, context);
        return code;
    };

    // ---- staging buffer with the pixel payload
    UploadGuard guard;
    guard.context = &context;

    VkBufferCreateInfo staging_info{};
    staging_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    staging_info.size = expected_bytes;
    staging_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    staging_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo staging_alloc{};
    staging_alloc.usage = VMA_MEMORY_USAGE_AUTO;
    staging_alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo staging_mapped{};
    if (vmaCreateBuffer(context.allocator, &staging_info, &staging_alloc, &guard.staging.buffer,
                        &guard.staging.allocation, &staging_mapped) != VK_SUCCESS) {
        return fail(ErrorCode::kOutOfMemory);
    }
    std::memcpy(staging_mapped.pMappedData, data.pixels.data(), expected_bytes);

    // ---- one-time commands: copy + mip ladder + final layout
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = context.queue_family;
    if (vkCreateCommandPool(context.device, &pool_info, nullptr, &guard.pool) != VK_SUCCESS) {
        return fail(ErrorCode::kUnknown);
    }

    VkCommandBufferAllocateInfo cmd_info{};
    cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_info.commandPool = guard.pool;
    cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_info.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(context.device, &cmd_info, &cmd) != VK_SUCCESS) {
        return fail(ErrorCode::kUnknown);
    }

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    image_barrier(cmd, texture.image, 0, texture.mip_levels, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                  VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {data.width, data.height, 1};
    vkCmdCopyBufferToImage(cmd, guard.staging.buffer, texture.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Blit ladder: level i-1 (TRANSFER_SRC) -> level i (TRANSFER_DST).
    std::int32_t mip_width = static_cast<std::int32_t>(data.width);
    std::int32_t mip_height = static_cast<std::int32_t>(data.height);
    for (std::uint32_t level = 1; level < texture.mip_levels; ++level) {
        image_barrier(cmd, texture.image, level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                      VK_ACCESS_2_TRANSFER_READ_BIT);

        const std::int32_t next_width = mip_width > 1 ? mip_width / 2 : 1;
        const std::int32_t next_height = mip_height > 1 ? mip_height / 2 : 1;

        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = level - 1;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[1] = {mip_width, mip_height, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = level;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[1] = {next_width, next_height, 1};
        vkCmdBlitImage(cmd, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        image_barrier(cmd, texture.image, level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                      VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        mip_width = next_width;
        mip_height = next_height;
    }
    // Last level never became a blit source; transition it directly.
    image_barrier(cmd, texture.image, texture.mip_levels - 1, 1,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(context.device, &fence_info, nullptr, &guard.fence) != VK_SUCCESS) {
        return fail(ErrorCode::kUnknown);
    }

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    if (vkQueueSubmit(context.queue, 1, &submit_info, guard.fence) != VK_SUCCESS ||
        vkWaitForFences(context.device, 1, &guard.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        return fail(ErrorCode::kUnknown);
    }

    // ---- view over the full chain
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = texture.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = texture.mip_levels;
    view_info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(context.device, &view_info, nullptr, &texture.view) != VK_SUCCESS) {
        return fail(ErrorCode::kUnknown);
    }

    return texture;
}

void texture_destroy(GpuTexture& texture, const ContextState& context) {
    if (texture.view != VK_NULL_HANDLE) {
        vkDestroyImageView(context.device, texture.view, nullptr);
        texture.view = VK_NULL_HANDLE;
    }
    if (texture.image != VK_NULL_HANDLE) {
        vmaDestroyImage(context.allocator, texture.image, texture.allocation);
        texture.image = VK_NULL_HANDLE;
        texture.allocation = VK_NULL_HANDLE;
    }
}

Result<VkSampler> sampler_cache_get(SamplerCache& cache, const ContextState& context,
                                    VkFilter filter, VkSamplerAddressMode address) {
    for (std::uint32_t i = 0; i < cache.count; ++i) {
        if (cache.entries[i].filter == filter && cache.entries[i].address == address) {
            return cache.entries[i].sampler;
        }
    }
    if (cache.count >= kMaxSamplers) {
        HUE_LOG_ERROR("sampler cache full (%u entries)", kMaxSamplers);
        return ErrorCode::kOutOfMemory;
    }

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = filter;
    sampler_info.minFilter = filter;
    sampler_info.mipmapMode = filter == VK_FILTER_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                         : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = address;
    sampler_info.addressModeV = address;
    sampler_info.addressModeW = address;
    sampler_info.maxLod = VK_LOD_CLAMP_NONE;

    VkSampler sampler = VK_NULL_HANDLE;
    HUE_VK_TRY(vkCreateSampler(context.device, &sampler_info, nullptr, &sampler));

    cache.entries[cache.count] = {filter, address, sampler};
    ++cache.count;
    return sampler;
}

void sampler_cache_destroy(SamplerCache& cache, const ContextState& context) {
    for (std::uint32_t i = 0; i < cache.count; ++i) {
        vkDestroySampler(context.device, cache.entries[i].sampler, nullptr);
        cache.entries[i].sampler = VK_NULL_HANDLE;
    }
    cache.count = 0;
}

} // namespace hue::render
