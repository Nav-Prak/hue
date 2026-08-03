// engine/render/include/hue/render/renderer.h
//
// Public renderer API (Week 4 spec). Vulkan stays entirely behind this
// header, same boundary rule as GLFW behind window.h: game code sees
// hue::Renderer, never a Vk* type. Instance/device/swapchain/VMA live in
// the internal State; dynamic rendering, 2 frames in flight.

#pragma once

#include <cstdint>

#include "hue/core/result.h"

namespace hue {

class Window;

struct RendererDesc {
    // Request VK_LAYER_KHRONOS_validation when it is installed; silently
    // skipped otherwise (CI machines have no layers).
    bool enable_validation = false;
    // Directory holding compiled .spv files. nullptr resolves to
    // "<executable dir>/shaders", where the build stages them.
    const char* shader_directory = nullptr;
};

struct RendererStatus {
    char adapter[256] = {};
    std::uint32_t swapchain_width = 0;
    std::uint32_t swapchain_height = 0;
    std::uint64_t frames_rendered = 0;
};

class Renderer {
public:
    // Fails with kUnsupported when no Vulkan 1.3 device is available (no
    // loader, no GPU, missing features); callers may treat that as
    // non-fatal and keep running without rendering.
    [[nodiscard]] static Result<Renderer> create(Window& window, const RendererDesc& desc = {});

    Renderer(Renderer&& other) noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer& operator=(Renderer&&) = delete;
    ~Renderer();

    // Records and submits one frame (clear + triangle). Swapchain
    // recreation on resize/out-of-date is handled internally; a minimized
    // window (0x0 framebuffer) is a successful no-op.
    [[nodiscard]] Result<void> draw_frame();

    // Runtime recompile hook: reloads .spv files from the shader directory
    // and rebuilds the pipeline. On any failure the previous pipeline stays
    // active and an error is returned (bad shader must never kill the run).
    [[nodiscard]] Result<void> reload_shaders();

    [[nodiscard]] const RendererStatus& status() const noexcept;

    // Opaque; defined in renderer.cpp. Public so internal helper functions
    // can name it -- the definition never leaves the module.
    struct State;

private:
    Renderer() = default;

    State* m_state = nullptr;
};

} // namespace hue
