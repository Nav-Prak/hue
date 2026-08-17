// engine/render/include/hue/render/renderer.h
//
// Public renderer API. Vulkan stays entirely behind this header, same
// boundary rule as GLFW behind window.h: game code sees hue::Renderer,
// never a Vk* type. Week 4: instance/device/swapchain/VMA, dynamic
// rendering, 2 frames in flight. Week 5: static meshes through staging
// buffers, depth attachment, camera + frustum-culled draw list.

#pragma once

#include <cstdint>

#include "hue/asset/mesh_data.h"
#include "hue/core/result.h"
#include "hue/render/camera.h"
#include "hue/render/mesh.h"

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
    std::uint32_t last_draws = 0;  // draw items rendered last frame
    std::uint32_t last_culled = 0; // draw items rejected by the frustum
};

// One instance of an uploaded mesh in the frame's draw list.
struct MeshDraw {
    MeshHandle mesh;
    Mat4 transform = Mat4::identity();
};

// Local light sources for the PBR pass (Week 6 spec: one directional +
// point lights). radius bounds the falloff window; intensity scales the
// linear-space color.
inline constexpr std::uint32_t kMaxPointLights = 4;

struct PointLight {
    Vec3 position{};
    float radius = 10.0f;
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
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

    // Uploads validated mesh data into device-local buffers via staging.
    // Synchronous: intended for load time, not mid-frame streaming.
    [[nodiscard]] Result<MeshHandle> upload_static_mesh(const asset::StaticMeshData& mesh);

    // Replaces the frame's point lights (up to kMaxPointLights; extras are
    // dropped with a log). The directional key light stays engine-managed
    // until the lighting pass grows in Week 16.
    void set_point_lights(const PointLight* lights, std::uint32_t count);

    // Records and submits one frame: draw items are frustum-culled against
    // their mesh bounds, depth-tested, and lit by the mesh pipeline. With
    // an empty list the Week 4 triangle draws instead, so a bare renderer
    // still proves the swapchain works. Swapchain recreation on resize/
    // out-of-date is handled internally; a minimized window is a no-op.
    [[nodiscard]] Result<void> draw_frame(const Camera& camera, const MeshDraw* draws,
                                          std::uint32_t draw_count);
    [[nodiscard]] Result<void> draw_frame(); // identity camera, empty list

    // Runtime recompile hook: reloads .spv files from the shader directory
    // and rebuilds all pipelines. On any failure the previous pipelines
    // stay active and an error is returned.
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
