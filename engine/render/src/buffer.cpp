// engine/render/src/buffer.cpp
//
// Device-local buffer creation via staging (Week 5 spec). Load-time path:
// synchronous copy with a transient command pool and a fence wait. The
// streaming version arrives when assets stream (not this phase).

#include "vk_types.h"

#include <cstring>

namespace hue::render {

namespace {

struct StagingGuard {
    const ContextState* context = nullptr;
    BufferAllocation staging;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    ~StagingGuard() {
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

} // namespace

Result<BufferAllocation> device_buffer_create(const ContextState& context, const void* data,
                                              std::size_t size, VkBufferUsageFlags usage) {
    if (data == nullptr || size == 0) {
        return ErrorCode::kInvalidArgument;
    }

    StagingGuard guard;
    guard.context = &context;

    // ---- staging buffer (host-visible, mapped at creation)
    VkBufferCreateInfo staging_info{};
    staging_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    staging_info.size = size;
    staging_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    staging_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo staging_alloc{};
    staging_alloc.usage = VMA_MEMORY_USAGE_AUTO;
    staging_alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo staging_mapped{};
    HUE_VK_TRY(vmaCreateBuffer(context.allocator, &staging_info, &staging_alloc,
                               &guard.staging.buffer, &guard.staging.allocation, &staging_mapped));
    std::memcpy(staging_mapped.pMappedData, data, size);

    // ---- device-local destination
    VkBufferCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    device_info.size = size;
    device_info.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    device_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo device_alloc{};
    device_alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    BufferAllocation destination;
    HUE_VK_TRY(vmaCreateBuffer(context.allocator, &device_info, &device_alloc,
                               &destination.buffer, &destination.allocation, nullptr));

    // ---- one-time copy. From here, failure must release destination too.
    auto fail = [&](VkResult result, const char* what) -> Result<BufferAllocation> {
        HUE_LOG_ERROR("vulkan: %s failed (VkResult %d)", what, static_cast<int>(result));
        vmaDestroyBuffer(context.allocator, destination.buffer, destination.allocation);
        return ErrorCode::kUnknown;
    };

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = context.queue_family;
    VkResult result = vkCreateCommandPool(context.device, &pool_info, nullptr, &guard.pool);
    if (result != VK_SUCCESS) {
        return fail(result, "vkCreateCommandPool");
    }

    VkCommandBufferAllocateInfo cmd_info{};
    cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_info.commandPool = guard.pool;
    cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_info.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    result = vkAllocateCommandBuffers(context.device, &cmd_info, &cmd);
    if (result != VK_SUCCESS) {
        return fail(result, "vkAllocateCommandBuffers");
    }

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);
    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(cmd, guard.staging.buffer, destination.buffer, 1, &region);
    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    result = vkCreateFence(context.device, &fence_info, nullptr, &guard.fence);
    if (result != VK_SUCCESS) {
        return fail(result, "vkCreateFence");
    }

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    result = vkQueueSubmit(context.queue, 1, &submit_info, guard.fence);
    if (result != VK_SUCCESS) {
        return fail(result, "vkQueueSubmit");
    }
    result = vkWaitForFences(context.device, 1, &guard.fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        return fail(result, "vkWaitForFences");
    }

    return destination;
}

void buffer_destroy(BufferAllocation& buffer, const ContextState& context) {
    if (buffer.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context.allocator, buffer.buffer, buffer.allocation);
        buffer.buffer = VK_NULL_HANDLE;
        buffer.allocation = VK_NULL_HANDLE;
    }
}

} // namespace hue::render
