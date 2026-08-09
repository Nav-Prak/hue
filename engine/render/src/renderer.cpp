// engine/render/src/renderer.cpp
//
// Public Renderer implementation: composes context/swapchain/pipeline and
// owns the frame loop. 2 frames in flight; image-available semaphore and
// fence per frame, render-finished semaphore per swapchain image (reusing
// one per frame trips validation when presents overlap).

#include "hue/render/renderer.h"

#include "vk_types.h"

#include "hue/core/memory.h"
#include "hue/core/trace.h"
#include "hue/core/window.h"

#include <GLFW/glfw3.h>

#include <cstring>
#include <new>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace hue {

using namespace hue::render;

namespace {

// Shader files stage next to the executable, and the working directory of
// tests/CI is anything but stable, so resolve relative to the binary.
void executable_shader_directory(char* buffer, std::size_t size) {
#if defined(_WIN32)
    const DWORD length = GetModuleFileNameA(nullptr, buffer, static_cast<DWORD>(size));
    buffer[(length < size) ? length : size - 1] = '\0';
    for (char* c = buffer; *c != '\0'; ++c) {
        if (*c == '\\') {
            *c = '/';
        }
    }
#else
    const ssize_t length = readlink("/proc/self/exe", buffer, size - 1);
    buffer[(length > 0) ? static_cast<std::size_t>(length) : 0] = '\0';
#endif
    char* last_slash = std::strrchr(buffer, '/');
    if (last_slash != nullptr) {
        *last_slash = '\0';
    }
    const std::size_t used = std::strlen(buffer);
    std::strncpy(buffer + used, "/shaders", size - used - 1);
}

} // namespace

struct Renderer::State {
    GLFWwindow* window = nullptr;
    ContextState context;
    SwapchainState swapchain;
    DepthState depth;
    PipelineState pipeline;
    MeshRegistry meshes;

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffers[kFramesInFlight] = {};
    VkSemaphore image_available[kFramesInFlight] = {};
    VkFence in_flight[kFramesInFlight] = {};
    VkSemaphore render_finished[kMaxSwapchainImages] = {};

    std::uint32_t frame_index = 0;
    bool swapchain_dirty = false;
    char shader_directory[512] = {};
    RendererStatus status;
};

namespace {

void destroy_sync_and_pool(Renderer::State& state) {
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (state.image_available[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(state.context.device, state.image_available[i], nullptr);
            state.image_available[i] = VK_NULL_HANDLE;
        }
        if (state.in_flight[i] != VK_NULL_HANDLE) {
            vkDestroyFence(state.context.device, state.in_flight[i], nullptr);
            state.in_flight[i] = VK_NULL_HANDLE;
        }
    }
    for (std::uint32_t i = 0; i < kMaxSwapchainImages; ++i) {
        if (state.render_finished[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(state.context.device, state.render_finished[i], nullptr);
            state.render_finished[i] = VK_NULL_HANDLE;
        }
    }
    if (state.command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(state.context.device, state.command_pool, nullptr);
        state.command_pool = VK_NULL_HANDLE;
    }
}

void state_destroy(Renderer::State& state) {
    if (state.context.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(state.context.device);
    }
    destroy_sync_and_pool(state);
    mesh_registry_destroy(state.meshes, state.context);
    pipeline_destroy(state.pipeline, state.context);
    depth_destroy(state.depth, state.context);
    swapchain_destroy(state.swapchain, state.context);
    context_destroy(state.context);
}

[[nodiscard]] Result<void> create_sync_and_pool(Renderer::State& state) {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = state.context.queue_family;
    HUE_VK_TRY(vkCreateCommandPool(state.context.device, &pool_info, nullptr,
                                   &state.command_pool));

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = state.command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = kFramesInFlight;
    HUE_VK_TRY(vkAllocateCommandBuffers(state.context.device, &alloc_info,
                                        state.command_buffers));

    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT; // first wait must not block

    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        HUE_VK_TRY(vkCreateSemaphore(state.context.device, &semaphore_info, nullptr,
                                     &state.image_available[i]));
        HUE_VK_TRY(vkCreateFence(state.context.device, &fence_info, nullptr,
                                 &state.in_flight[i]));
    }
    for (std::uint32_t i = 0; i < kMaxSwapchainImages; ++i) {
        HUE_VK_TRY(vkCreateSemaphore(state.context.device, &semaphore_info, nullptr,
                                     &state.render_finished[i]));
    }
    return {};
}

[[nodiscard]] Result<void> recreate_swapchain(Renderer::State& state) {
    vkDeviceWaitIdle(state.context.device);
    swapchain_destroy(state.swapchain, state.context);
    depth_destroy(state.depth, state.context);
    const auto created = swapchain_create(state.swapchain, state.context, state.window);
    if (!created) {
        return created.error();
    }
    const auto depth = depth_create(state.depth, state.context, state.swapchain.extent);
    if (!depth) {
        return depth.error();
    }
    state.swapchain_dirty = false;
    state.status.swapchain_width = state.swapchain.extent.width;
    state.status.swapchain_height = state.swapchain.extent.height;
    return {};
}

[[nodiscard]] Result<void> record_frame(Renderer::State& state, VkCommandBuffer cmd,
                                        std::uint32_t image_index, const Camera& camera,
                                        const MeshDraw* draws, std::uint32_t draw_count) {
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    HUE_VK_TRY(vkBeginCommandBuffer(cmd, &begin_info));

    // UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL (contents are cleared anyway).
    VkImageMemoryBarrier2 to_color{};
    to_color.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    to_color.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    to_color.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    to_color.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    to_color.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_color.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_color.image = state.swapchain.images[image_index];
    to_color.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_color.subresourceRange.levelCount = 1;
    to_color.subresourceRange.layerCount = 1;

    // Depth: UNDEFINED -> DEPTH_ATTACHMENT (cleared each frame, so the
    // previous contents are never needed).
    VkImageMemoryBarrier2 to_depth{};
    to_depth.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    to_depth.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    to_depth.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    to_depth.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    to_depth.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_depth.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    to_depth.image = state.depth.image;
    to_depth.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    to_depth.subresourceRange.levelCount = 1;
    to_depth.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier2 begin_barriers[] = {to_color, to_depth};
    VkDependencyInfo to_color_dep{};
    to_color_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    to_color_dep.imageMemoryBarrierCount = 2;
    to_color_dep.pImageMemoryBarriers = begin_barriers;
    vkCmdPipelineBarrier2(cmd, &to_color_dep);

    VkRenderingAttachmentInfo color_attachment{};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = state.swapchain.views[image_index];
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue.color = {{0.055f, 0.06f, 0.09f, 1.0f}}; // hue night blue

    VkRenderingAttachmentInfo depth_attachment{};
    depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_attachment.imageView = state.depth.view;
    depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.extent = state.swapchain.extent;
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;
    rendering_info.pDepthAttachment = &depth_attachment;

    vkCmdBeginRendering(cmd, &rendering_info);

    // Negative viewport height flips Vulkan's Y-down clip space back to the
    // engine's Y-up convention; front faces stay counter-clockwise.
    VkViewport viewport{};
    viewport.y = static_cast<float>(state.swapchain.extent.height);
    viewport.width = static_cast<float>(state.swapchain.extent.width);
    viewport.height = -static_cast<float>(state.swapchain.extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = state.swapchain.extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    std::uint32_t drawn = 0;
    std::uint32_t culled = 0;
    if (draw_count == 0) {
        // Bare renderer (renderer_smoke, pre-scene startup): the Week 4
        // triangle still proves the pipeline end to end.
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline.triangle);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    } else {
        const Mat4 view_projection = camera.view_projection();
        const Frustum frustum = Frustum::from_view_projection(view_projection);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline.mesh);
        for (std::uint32_t d = 0; d < draw_count; ++d) {
            const GpuMesh* mesh = mesh_registry_resolve(state.meshes, draws[d].mesh);
            if (mesh == nullptr) {
                continue; // stale handle; upload path already logged
            }
            if (!frustum.intersects(mesh->bounds.transformed(draws[d].transform))) {
                ++culled;
                continue;
            }

            const VkDeviceSize zero_offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->vertices.buffer, &zero_offset);
            vkCmdBindIndexBuffer(cmd, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32);

            for (std::size_t i = 0; i < mesh->instances.size(); ++i) {
                const asset::MeshInstance& instance = mesh->instances[i];
                const asset::MeshPrimitive& primitive =
                    mesh->primitives[instance.primitive_index];

                MeshPushConstants push;
                push.model = draws[d].transform * instance.transform;
                push.mvp = view_projection * push.model;
                vkCmdPushConstants(cmd, state.pipeline.mesh_layout, VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(push), &push);
                vkCmdDrawIndexed(cmd, primitive.index_count, 1, primitive.first_index,
                                 primitive.vertex_offset, 0);
            }
            ++drawn;
        }
    }
    state.status.last_draws = drawn;
    state.status.last_culled = culled;

    vkCmdEndRendering(cmd);

    // COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC.
    VkImageMemoryBarrier2 to_present = to_color;
    to_present.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    to_present.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    to_present.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    to_present.dstAccessMask = 0;
    to_present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkDependencyInfo to_present_dep{};
    to_present_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    to_present_dep.imageMemoryBarrierCount = 1;
    to_present_dep.pImageMemoryBarriers = &to_present;
    vkCmdPipelineBarrier2(cmd, &to_present_dep);

    HUE_VK_TRY(vkEndCommandBuffer(cmd));
    return {};
}

} // namespace

Result<Renderer> Renderer::create(Window& window, const RendererDesc& desc) {
    auto allocation = heap_allocate(sizeof(State), alignof(State), MemoryTag::kRender);
    if (!allocation) {
        return allocation.error();
    }
    State* state = ::new (allocation.value()) State();
    state->window = window.handle();

    if (desc.shader_directory != nullptr) {
        std::strncpy(state->shader_directory, desc.shader_directory,
                     sizeof(state->shader_directory) - 1);
    } else {
        executable_shader_directory(state->shader_directory, sizeof(state->shader_directory));
    }

    auto fail = [state](ErrorCode code) -> Result<Renderer> {
        state_destroy(*state);
        state->~State();
        (void)heap_free(state);
        return code;
    };

    const auto context = context_create(state->context, state->window, desc.enable_validation);
    if (!context) {
        return fail(context.error());
    }
    const auto swapchain = swapchain_create(state->swapchain, state->context, state->window);
    if (!swapchain) {
        return fail(swapchain.error());
    }
    const auto depth = depth_create(state->depth, state->context, state->swapchain.extent);
    if (!depth) {
        return fail(depth.error());
    }
    const auto pipeline =
        pipeline_create(state->pipeline, state->context, state->swapchain.format,
                        state->depth.format, state->shader_directory);
    if (!pipeline) {
        return fail(pipeline.error());
    }
    const auto sync = create_sync_and_pool(*state);
    if (!sync) {
        return fail(sync.error());
    }

    std::strncpy(state->status.adapter, state->context.adapter_name,
                 sizeof(state->status.adapter) - 1);
    state->status.swapchain_width = state->swapchain.extent.width;
    state->status.swapchain_height = state->swapchain.extent.height;

    HUE_LOG_INFO("renderer initialized: %s, shaders at %s", state->status.adapter,
                 state->shader_directory);

    Renderer renderer;
    renderer.m_state = state;
    return renderer;
}

Renderer::Renderer(Renderer&& other) noexcept : m_state(other.m_state) {
    other.m_state = nullptr;
}

Renderer::~Renderer() {
    if (m_state != nullptr) {
        state_destroy(*m_state);
        m_state->~State();
        (void)heap_free(m_state);
        m_state = nullptr;
    }
}

Result<MeshHandle> Renderer::upload_static_mesh(const asset::StaticMeshData& mesh) {
    HUE_PROFILE_ZONE("Renderer::upload_static_mesh");
    return mesh_registry_upload(m_state->meshes, m_state->context, mesh);
}

Result<void> Renderer::draw_frame() {
    return draw_frame(Camera{}, nullptr, 0);
}

Result<void> Renderer::draw_frame(const Camera& camera, const MeshDraw* draws,
                                  std::uint32_t draw_count) {
    HUE_PROFILE_ZONE("Renderer::draw_frame");
    State& state = *m_state;

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(state.window, &width, &height);
    if (width == 0 || height == 0) {
        return {}; // minimized: nothing to do, not an error
    }

    if (state.swapchain_dirty ||
        static_cast<std::uint32_t>(width) != state.swapchain.extent.width ||
        static_cast<std::uint32_t>(height) != state.swapchain.extent.height) {
        const auto recreated = recreate_swapchain(state);
        if (!recreated) {
            return recreated.error();
        }
    }

    const std::uint32_t frame = state.frame_index;
    HUE_VK_TRY(vkWaitForFences(state.context.device, 1, &state.in_flight[frame], VK_TRUE,
                               UINT64_MAX));

    std::uint32_t image_index = 0;
    const VkResult acquire =
        vkAcquireNextImageKHR(state.context.device, state.swapchain.swapchain, UINT64_MAX,
                              state.image_available[frame], VK_NULL_HANDLE, &image_index);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        state.swapchain_dirty = true;
        return {}; // recreate at the top of the next frame
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        HUE_LOG_ERROR("vkAcquireNextImageKHR failed (VkResult %d)", static_cast<int>(acquire));
        return ErrorCode::kUnknown;
    }

    HUE_VK_TRY(vkResetFences(state.context.device, 1, &state.in_flight[frame]));

    VkCommandBuffer cmd = state.command_buffers[frame];
    HUE_VK_TRY(vkResetCommandBuffer(cmd, 0));
    const auto recorded = record_frame(state, cmd, image_index, camera, draws, draw_count);
    if (!recorded) {
        return recorded.error();
    }

    VkSemaphoreSubmitInfo wait_info{};
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait_info.semaphore = state.image_available[frame];
    wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signal_info{};
    signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_info.semaphore = state.render_finished[image_index];
    signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo cmd_info{};
    cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmd_info.commandBuffer = cmd;

    VkSubmitInfo2 submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.waitSemaphoreInfoCount = 1;
    submit_info.pWaitSemaphoreInfos = &wait_info;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &cmd_info;
    submit_info.signalSemaphoreInfoCount = 1;
    submit_info.pSignalSemaphoreInfos = &signal_info;

    HUE_VK_TRY(vkQueueSubmit2(state.context.queue, 1, &submit_info, state.in_flight[frame]));

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &state.render_finished[image_index];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &state.swapchain.swapchain;
    present_info.pImageIndices = &image_index;

    const VkResult present = vkQueuePresentKHR(state.context.queue, &present_info);
    if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR) {
        state.swapchain_dirty = true;
    } else if (present != VK_SUCCESS) {
        HUE_LOG_ERROR("vkQueuePresentKHR failed (VkResult %d)", static_cast<int>(present));
        return ErrorCode::kUnknown;
    }

    state.frame_index = (frame + 1) % kFramesInFlight;
    ++state.status.frames_rendered;
    return {};
}

Result<void> Renderer::reload_shaders() {
    State& state = *m_state;

    // Build the replacement first; only touch the live pipeline on success.
    PipelineState fresh;
    const auto created = pipeline_create(fresh, state.context, state.swapchain.format,
                                         state.depth.format, state.shader_directory);
    if (!created) {
        pipeline_destroy(fresh, state.context);
        HUE_LOG_WARN("shader reload failed; previous pipeline stays active");
        return created.error();
    }

    vkDeviceWaitIdle(state.context.device);
    pipeline_destroy(state.pipeline, state.context);
    state.pipeline = fresh;
    HUE_LOG_INFO("shader reload complete");
    return {};
}

const RendererStatus& Renderer::status() const noexcept {
    return m_state->status;
}

} // namespace hue
