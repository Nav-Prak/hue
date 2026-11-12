// engine/render/src/shadow.cpp
//
// One directional shadow map: a square D32 image rendered depth-only from
// the key light each frame and sampled by the mesh fragment shader through
// a comparison sampler. The image is created once and never resized; its
// layout cycles attachment -> read-only inside every recorded frame.

#include "vk_types.h"

#include <cmath>

namespace hue {

Mat4 directional_shadow_matrix(Vec3 direction, Vec3 center, float radius) noexcept {
    const bool finite_direction =
        std::isfinite(direction.x) && std::isfinite(direction.y) && std::isfinite(direction.z);
    if (!finite_direction || length_squared(direction) < 1.0e-8f) {
        direction = {0.0f, -1.0f, 0.0f};
    }
    direction = normalize(direction);
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) {
        center = {};
    }
    if (!std::isfinite(radius) || radius < 1.0f) {
        radius = 1.0f;
    }
    if (radius > 512.0f) {
        radius = 512.0f;
    }
    // look_at degenerates when the view direction is parallel to its up hint.
    const Vec3 up = std::abs(direction.y) > 0.99f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
    const Vec3 eye = center - direction * (radius * 2.0f);
    const Mat4 view = Mat4::look_at(eye, center, up);
    // The sphere spans view depths [radius, 3 * radius]; pad both ends.
    const Mat4 projection =
        Mat4::orthographic(-radius, radius, -radius, radius, radius * 0.5f, radius * 3.5f);
    return projection * view;
}

namespace render {

Result<void> shadow_create(ShadowState& shadow, const ContextState& context, std::uint32_t size) {
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = shadow.format;
    image_info.extent = {size, size, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    HUE_VK_TRY(vmaCreateImage(context.allocator, &image_info, &alloc_info, &shadow.image,
                              &shadow.allocation, nullptr));

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = shadow.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = shadow.format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    HUE_VK_TRY(vkCreateImageView(context.device, &view_info, nullptr, &shadow.view));

    // Linear filtering of a depth comparison gives 2x2 PCF for free, but it is
    // an optional format feature; fall back to nearest where it is missing.
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context.physical_device, shadow.format, &properties);
    const bool linear = (properties.optimalTilingFeatures &
                         VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sampler_info.minFilter = sampler_info.magFilter;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    // Outside the map compares against the far plane: always lit.
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sampler_info.compareEnable = VK_TRUE;
    sampler_info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    HUE_VK_TRY(vkCreateSampler(context.device, &sampler_info, nullptr, &shadow.sampler));

    shadow.size = size;
    return {};
}

void shadow_destroy(ShadowState& shadow, const ContextState& context) {
    if (shadow.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(context.device, shadow.sampler, nullptr);
        shadow.sampler = VK_NULL_HANDLE;
    }
    if (shadow.view != VK_NULL_HANDLE) {
        vkDestroyImageView(context.device, shadow.view, nullptr);
        shadow.view = VK_NULL_HANDLE;
    }
    if (shadow.image != VK_NULL_HANDLE) {
        vmaDestroyImage(context.allocator, shadow.image, shadow.allocation);
        shadow.image = VK_NULL_HANDLE;
        shadow.allocation = VK_NULL_HANDLE;
    }
    shadow.size = 0;
}

} // namespace render
} // namespace hue
