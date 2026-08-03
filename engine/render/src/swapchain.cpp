// engine/render/src/swapchain.cpp
//
// Swapchain + image views. Full recreate on resize/out-of-date (after
// device idle); the incremental oldSwapchain path is an optimization for
// later, not bring-up.

#include "vk_types.h"

#include <GLFW/glfw3.h>

namespace hue::render {

namespace {

[[nodiscard]] VkSurfaceFormatKHR pick_surface_format(VkPhysicalDevice device,
                                                     VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr);
    VkSurfaceFormatKHR formats[128];
    if (count > 128) {
        count = 128;
    }
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, formats);

    // sRGB output so lighting math (Week 6 PBR) stays linear-correct.
    for (std::uint32_t i = 0; i < count; ++i) {
        if ((formats[i].format == VK_FORMAT_B8G8R8A8_SRGB ||
             formats[i].format == VK_FORMAT_R8G8B8A8_SRGB) &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return formats[i];
        }
    }
    return formats[0];
}

} // namespace

Result<void> swapchain_create(SwapchainState& swapchain, const ContextState& context,
                              GLFWwindow* window) {
    VkSurfaceCapabilitiesKHR capabilities;
    HUE_VK_TRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.physical_device, context.surface,
                                                         &capabilities));

    int framebuffer_width = 0;
    int framebuffer_height = 0;
    glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == 0xFFFFFFFFu) { // surface lets the swapchain decide
        extent.width = static_cast<std::uint32_t>(framebuffer_width);
        extent.height = static_cast<std::uint32_t>(framebuffer_height);
    }
    if (extent.width < capabilities.minImageExtent.width) {
        extent.width = capabilities.minImageExtent.width;
    }
    if (extent.width > capabilities.maxImageExtent.width) {
        extent.width = capabilities.maxImageExtent.width;
    }
    if (extent.height < capabilities.minImageExtent.height) {
        extent.height = capabilities.minImageExtent.height;
    }
    if (extent.height > capabilities.maxImageExtent.height) {
        extent.height = capabilities.maxImageExtent.height;
    }
    if (extent.width == 0 || extent.height == 0) {
        return ErrorCode::kInvalidArgument; // minimized; caller skips the frame
    }

    const VkSurfaceFormatKHR surface_format =
        pick_surface_format(context.physical_device, context.surface);

    std::uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }
    if (image_count > kMaxSwapchainImages) {
        image_count = kMaxSwapchainImages;
    }

    VkSwapchainCreateInfoKHR swapchain_info{};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = context.surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = surface_format.format;
    swapchain_info.imageColorSpace = surface_format.colorSpace;
    swapchain_info.imageExtent = extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = capabilities.currentTransform;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR; // vsync; always available
    swapchain_info.clipped = VK_TRUE;

    HUE_VK_TRY(vkCreateSwapchainKHR(context.device, &swapchain_info, nullptr,
                                    &swapchain.swapchain));

    swapchain.format = surface_format.format;
    swapchain.extent = extent;

    std::uint32_t actual_count = 0;
    HUE_VK_TRY(vkGetSwapchainImagesKHR(context.device, swapchain.swapchain, &actual_count,
                                       nullptr));
    if (actual_count > kMaxSwapchainImages) {
        actual_count = kMaxSwapchainImages;
    }
    HUE_VK_TRY(vkGetSwapchainImagesKHR(context.device, swapchain.swapchain, &actual_count,
                                       swapchain.images));
    swapchain.image_count = actual_count;

    for (std::uint32_t i = 0; i < swapchain.image_count; ++i) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = swapchain.images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = swapchain.format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        HUE_VK_TRY(vkCreateImageView(context.device, &view_info, nullptr, &swapchain.views[i]));
    }

    HUE_LOG_INFO("swapchain: %ux%u, %u images, format %d", extent.width, extent.height,
                 swapchain.image_count, static_cast<int>(swapchain.format));
    return {};
}

void swapchain_destroy(SwapchainState& swapchain, const ContextState& context) {
    for (std::uint32_t i = 0; i < swapchain.image_count; ++i) {
        if (swapchain.views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(context.device, swapchain.views[i], nullptr);
            swapchain.views[i] = VK_NULL_HANDLE;
        }
    }
    if (swapchain.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(context.device, swapchain.swapchain, nullptr);
        swapchain.swapchain = VK_NULL_HANDLE;
    }
    swapchain.image_count = 0;
}

} // namespace hue::render
