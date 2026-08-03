// engine/render/src/vk_types.h
//
// Internal shared state for the render module. Never included from public
// headers. C-style module split: context / swapchain / pipeline each own a
// state struct plus create/destroy functions; renderer.cpp composes them.

#pragma once

#include <volk.h>

#include <vk_mem_alloc.h>

#include "hue/core/log.h"
#include "hue/core/result.h"

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

struct PipelineState {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

// context.cpp
Result<void> context_create(ContextState& context, GLFWwindow* window, bool enable_validation);
void context_destroy(ContextState& context);

// swapchain.cpp
Result<void> swapchain_create(SwapchainState& swapchain, const ContextState& context,
                              GLFWwindow* window);
void swapchain_destroy(SwapchainState& swapchain, const ContextState& context);

// pipeline.cpp
Result<void> pipeline_create(PipelineState& pipeline, const ContextState& context,
                             VkFormat color_format, const char* shader_directory);
void pipeline_destroy(PipelineState& pipeline, const ContextState& context);

} // namespace hue::render
