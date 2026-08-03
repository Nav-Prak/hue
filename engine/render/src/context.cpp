// engine/render/src/context.cpp
//
// Instance, surface, device selection, queue, and VMA allocator. Vulkan
// 1.3 with dynamic rendering + synchronization2 is the hard floor: no
// render-pass objects, sync2 barriers everywhere.

#include "vk_types.h"

#include <GLFW/glfw3.h>

#include <cstring>

namespace hue::render {

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debug_messenger_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* user_data) {
    (void)type;
    (void)user_data;
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        HUE_LOG_ERROR("vulkan validation: %s", data->pMessage);
    } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        HUE_LOG_WARN("vulkan validation: %s", data->pMessage);
    }
    return VK_FALSE;
}

[[nodiscard]] bool validation_layer_available() {
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }
    VkLayerProperties layers[64];
    if (count > 64) {
        count = 64;
    }
    if (vkEnumerateInstanceLayerProperties(&count, layers) != VK_SUCCESS) {
        return false;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        if (std::strcmp(layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            return true;
        }
    }
    return false;
}

// Requirements: API 1.3, dynamicRendering + synchronization2, swapchain
// extension, and one queue family doing both graphics and present.
// Returns a score (higher = better) or 0 if unusable.
[[nodiscard]] std::uint32_t score_device(VkPhysicalDevice device, VkSurfaceKHR surface,
                                         std::uint32_t& out_queue_family) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_3) {
        return 0;
    }

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features13;
    vkGetPhysicalDeviceFeatures2(device, &features2);
    if (features13.dynamicRendering != VK_TRUE || features13.synchronization2 != VK_TRUE) {
        return 0;
    }

    // VK_KHR_swapchain support.
    std::uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);
    bool has_swapchain = false;
    if (extension_count > 0 && extension_count <= 1024) {
        static VkExtensionProperties extensions[1024]; // init-time only
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, extensions);
        for (std::uint32_t i = 0; i < extension_count; ++i) {
            if (std::strcmp(extensions[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                has_swapchain = true;
                break;
            }
        }
    }
    if (!has_swapchain) {
        return 0;
    }

    // Combined graphics + present family. Separate-family GPUs exist but
    // are rare; supporting them is complexity Week 4 does not need.
    std::uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
    VkQueueFamilyProperties families[64];
    if (family_count > 64) {
        family_count = 64;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families);
    bool family_found = false;
    for (std::uint32_t i = 0; i < family_count; ++i) {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
            continue;
        }
        VkBool32 present_supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_supported);
        if (present_supported == VK_TRUE) {
            out_queue_family = i;
            family_found = true;
            break;
        }
    }
    if (!family_found) {
        return 0;
    }

    switch (properties.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return 1000;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return 500;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return 250;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return 100; // lavapipe: how CI renders
    default:
        return 50;
    }
}

} // namespace

Result<void> context_create(ContextState& context, GLFWwindow* window, bool enable_validation) {
    if (volkInitialize() != VK_SUCCESS) {
        HUE_LOG_WARN("vulkan loader not found on this machine");
        return ErrorCode::kUnsupported;
    }
    if (glfwVulkanSupported() != GLFW_TRUE) {
        HUE_LOG_WARN("GLFW reports no Vulkan support");
        return ErrorCode::kUnsupported;
    }

    // ---- instance
    std::uint32_t glfw_extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    if (glfw_extensions == nullptr || glfw_extension_count == 0) {
        HUE_LOG_WARN("GLFW returned no required Vulkan instance extensions");
        return ErrorCode::kUnsupported;
    }

    const char* extensions[16];
    std::uint32_t extension_count = 0;
    for (std::uint32_t i = 0; i < glfw_extension_count && extension_count < 15; ++i) {
        extensions[extension_count] = glfw_extensions[i];
        ++extension_count;
    }

    const bool use_validation = enable_validation && validation_layer_available();
    const char* layers[1] = {"VK_LAYER_KHRONOS_validation"};
    if (use_validation) {
        extensions[extension_count] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        ++extension_count;
    }

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "hue";
    app_info.pEngineName = "hue";
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instance_info{};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledExtensionCount = extension_count;
    instance_info.ppEnabledExtensionNames = extensions;
    instance_info.enabledLayerCount = use_validation ? 1u : 0u;
    instance_info.ppEnabledLayerNames = use_validation ? layers : nullptr;

    const VkResult instance_result = vkCreateInstance(&instance_info, nullptr, &context.instance);
    if (instance_result != VK_SUCCESS) {
        HUE_LOG_WARN("vkCreateInstance failed (VkResult %d)", static_cast<int>(instance_result));
        return ErrorCode::kUnsupported;
    }
    volkLoadInstance(context.instance);

    if (use_validation) {
        VkDebugUtilsMessengerCreateInfoEXT messenger_info{};
        messenger_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messenger_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messenger_info.pfnUserCallback = &debug_messenger_callback;
        HUE_VK_TRY(vkCreateDebugUtilsMessengerEXT(context.instance, &messenger_info, nullptr,
                                                  &context.debug_messenger));
        HUE_LOG_INFO("vulkan validation layer enabled");
    }

    // ---- surface
    HUE_VK_TRY(glfwCreateWindowSurface(context.instance, window, nullptr, &context.surface));

    // ---- physical device
    std::uint32_t device_count = 0;
    HUE_VK_TRY(vkEnumeratePhysicalDevices(context.instance, &device_count, nullptr));
    if (device_count == 0) {
        HUE_LOG_WARN("no Vulkan physical devices present");
        return ErrorCode::kUnsupported;
    }
    VkPhysicalDevice devices[16];
    if (device_count > 16) {
        device_count = 16;
    }
    HUE_VK_TRY(vkEnumeratePhysicalDevices(context.instance, &device_count, devices));

    std::uint32_t best_score = 0;
    for (std::uint32_t i = 0; i < device_count; ++i) {
        std::uint32_t queue_family = 0;
        const std::uint32_t score = score_device(devices[i], context.surface, queue_family);
        if (score > best_score) {
            best_score = score;
            context.physical_device = devices[i];
            context.queue_family = queue_family;
        }
    }
    if (best_score == 0) {
        HUE_LOG_WARN("no Vulkan 1.3 device with dynamic rendering + present support");
        return ErrorCode::kUnsupported;
    }

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(context.physical_device, &properties);
    std::strncpy(context.adapter_name, properties.deviceName, sizeof(context.adapter_name) - 1);
    HUE_LOG_INFO("vulkan adapter: %s (api %u.%u.%u)", properties.deviceName,
                 VK_API_VERSION_MAJOR(properties.apiVersion),
                 VK_API_VERSION_MINOR(properties.apiVersion),
                 VK_API_VERSION_PATCH(properties.apiVersion));

    // ---- logical device + queue
    const float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = context.queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = &features13;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = device_extensions;

    HUE_VK_TRY(vkCreateDevice(context.physical_device, &device_info, nullptr, &context.device));
    volkLoadDevice(context.device);
    vkGetDeviceQueue(context.device, context.queue_family, 0, &context.queue);

    // ---- VMA (dynamic function fetch: volk means no static vulkan symbols)
    VmaVulkanFunctions vma_functions{};
    vma_functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vma_functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocator_info{};
    allocator_info.vulkanApiVersion = VK_API_VERSION_1_3;
    allocator_info.physicalDevice = context.physical_device;
    allocator_info.device = context.device;
    allocator_info.instance = context.instance;
    allocator_info.pVulkanFunctions = &vma_functions;
    HUE_VK_TRY(vmaCreateAllocator(&allocator_info, &context.allocator));

    return {};
}

void context_destroy(ContextState& context) {
    if (context.allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(context.allocator);
        context.allocator = VK_NULL_HANDLE;
    }
    if (context.device != VK_NULL_HANDLE) {
        vkDestroyDevice(context.device, nullptr);
        context.device = VK_NULL_HANDLE;
    }
    if (context.surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(context.instance, context.surface, nullptr);
        context.surface = VK_NULL_HANDLE;
    }
    if (context.debug_messenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(context.instance, context.debug_messenger, nullptr);
        context.debug_messenger = VK_NULL_HANDLE;
    }
    if (context.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(context.instance, nullptr);
        context.instance = VK_NULL_HANDLE;
    }
}

} // namespace hue::render
