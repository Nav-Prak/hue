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
    std::uint32_t live_pipelines = 0, live_layouts = 0, live_shader_modules = 0;
    std::uint64_t shader_generation = 0;
    std::uint32_t shadow_map_size = 0;   // texels per side; 0 without a renderer
    std::uint32_t last_shadow_draws = 0; // draw items rendered into the shadow map
};

// One instance of an uploaded mesh in the frame's draw list.
struct MeshDraw {
    MeshHandle mesh;
    Mat4 transform = Mat4::identity();
    // Required only for meshes uploaded through upload_skinned_mesh. The
    // palette is copied into the renderer's per-frame GPU buffer before draw.
    const Mat4* joint_matrices = nullptr;
    std::uint32_t joint_count = 0;
    // Multiplies every material's base color for this instance (scene props
    // share one mesh and differ only by tint).
    Vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
};

// Local light sources for the PBR pass (Week 6 spec: one directional +
// point lights). radius bounds the falloff window; intensity scales the
// linear-space color.
inline constexpr std::uint32_t kMaxPointLights = 4;
inline constexpr std::uint32_t kMaxHudQuads = 8192;

// Screen-space colored rectangle in engine NDC (Y-up, matching the
// negative-viewport 3D pass). Drawn after meshes with depth off.
struct HudQuad {
    Vec2 ndc_min{};
    Vec2 ndc_max{};
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    // Off-diagonal basis for projected debug lines; zero keeps rectangles.
    Vec2 skew{};
};

struct PointLight {
    Vec3 position{};
    float radius = 10.0f;
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

// The one directional key light, plus flat ambient. When cast_shadows is set
// it renders a single shadow map covering a sphere of shadow_radius around
// shadow_center; anything outside that sphere is simply lit.
struct DirectionalLight {
    Vec3 direction{0.4f, -1.0f, 0.3f}; // direction the light travels
    Vec3 color{1.0f, 0.96f, 0.9f};
    float intensity = 3.0f;
    Vec3 ambient{0.05f, 0.055f, 0.07f};
    bool cast_shadows = true;
    Vec3 shadow_center{};
    float shadow_radius = 16.0f;
};

inline constexpr std::uint32_t kShadowMapSize = 2048;
inline constexpr std::uint32_t kMaxFrameDraws = 256;

// World -> shadow clip space for a directional light: an orthographic box
// looking along `direction` at `center`, sized to hold a sphere of `radius`.
// Pure and total: a zero, vertical or non-finite direction, or a non-positive
// or non-finite radius, falls back to a safe value instead of producing NaNs.
[[nodiscard]] Mat4 directional_shadow_matrix(Vec3 direction, Vec3 center, float radius) noexcept;

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
    [[nodiscard]] Result<MeshHandle> upload_skinned_mesh(const asset::SkinnedMeshData& mesh);

    // Replaces the frame's point lights (up to kMaxPointLights; extras are
    // dropped with a log).
    void set_point_lights(const PointLight* lights, std::uint32_t count);
    // Replaces the key light. Non-finite or out-of-range values are replaced
    // by the defaults rather than reaching the GPU.
    void set_directional_light(const DirectionalLight& light);

    // Records and submits one frame: every draw item first renders depth into
    // the shadow map, then items are frustum-culled against their mesh
    // bounds, depth-tested, and lit by the mesh pipeline. At most
    // kMaxFrameDraws items are used; extras are dropped with a log. With
    // an empty list the Week 4 triangle draws instead, so a bare renderer
    // still proves the swapchain works. Swapchain recreation on resize/
    // out-of-date is handled internally; a minimized window is a no-op.
    [[nodiscard]] Result<void> draw_frame(const Camera& camera, const MeshDraw* draws,
                                          std::uint32_t draw_count,
                                          const HudQuad* hud = nullptr,
                                          std::uint32_t hud_count = 0);
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
