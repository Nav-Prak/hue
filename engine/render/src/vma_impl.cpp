// engine/render/src/vma_impl.cpp
//
// Single TU housing the volk and VMA implementations so both compile with
// engine flags. VMA fetches Vulkan functions dynamically (volk provides no
// static symbols to link against).

#define VOLK_IMPLEMENTATION
#include <volk.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
