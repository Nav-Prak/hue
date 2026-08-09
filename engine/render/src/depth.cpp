// engine/render/src/depth.cpp
//
// Depth attachment (Week 5 spec). One D32 image sized to the swapchain,
// recreated with it on resize. Layout transitions happen in record_frame.

#include "vk_types.h"

namespace hue::render {

Result<void> depth_create(DepthState& depth, const ContextState& context, VkExtent2D extent) {
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = depth.format;
    image_info.extent = {extent.width, extent.height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT; // full-frame attachment

    HUE_VK_TRY(vmaCreateImage(context.allocator, &image_info, &alloc_info, &depth.image,
                              &depth.allocation, nullptr));

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = depth.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = depth.format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    HUE_VK_TRY(vkCreateImageView(context.device, &view_info, nullptr, &depth.view));

    depth.extent = extent;
    return {};
}

void depth_destroy(DepthState& depth, const ContextState& context) {
    if (depth.view != VK_NULL_HANDLE) {
        vkDestroyImageView(context.device, depth.view, nullptr);
        depth.view = VK_NULL_HANDLE;
    }
    if (depth.image != VK_NULL_HANDLE) {
        vmaDestroyImage(context.allocator, depth.image, depth.allocation);
        depth.image = VK_NULL_HANDLE;
        depth.allocation = VK_NULL_HANDLE;
    }
    depth.extent = {0, 0};
}

} // namespace hue::render
