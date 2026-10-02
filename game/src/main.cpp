// game/src/main.cpp
//
// Week 1 loop: window + input + fixed timestep. Week 2 adds a fixed-storage
// allocation reporter until ImGui becomes the visual overlay in Week 13.
// Week 3 spins up the job pool and runs a startup parallel-for benchmark.
// Week 4 brings up the Vulkan renderer (clear + triangle); machines with no
// Vulkan 1.3 device keep running windowed unless --require-renderer is set.
// Week 5 uploads static meshes (procedural scene + a cube through the
// validated glTF path) and flies a debug camera through them.
// --frames N exits after N frames (CI smoke test).
// Week 12 routes gameplay through Encounter; --encounter-smoke checks 20
// reset/outcome cycles. Bitmap prompts and optional joint-volume diagnostics
// use the existing HUD pass. Rendering does not advance gameplay state.

#include "characters.h"
#include "combat_character.h"
#include "combat_hud.h"
#include "encounter.h"
#include "fight_report.h"
#include "fly_camera.h"
#include "follow_camera.h"
#include "runtime_files.h"
#include "test_meshes.h"

#include "hue/anim/animation.h"
#include "hue/combat/ai.h"
#include "hue/combat/combat.h"
#include "hue/core/debug_channel.h"
#include "hue/core/input.h"
#include "hue/core/jobs.h"
#include "hue/core/log.h"
#include "hue/core/math.h"
#include "hue/core/memory.h"
#include "hue/core/spring.h"
#include "hue/core/time.h"
#include "hue/core/trace.h"
#include "hue/core/version.h"
#include "hue/core/window.h"
#include "hue/ecs/ecs.h"
#include "hue/physics/physics.h"
#include "hue/render/camera.h"
#include "hue/render/renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>

namespace {

void log_input_edges(const hue::Input& input) {
    using namespace hue;

    for (int b = 0; b < 15; ++b) {
        if (input.pad_pressed(b)) {
            HUE_LOG_INFO("gamepad button %d pressed", b);
        }
    }
    const GamepadState& pad = input.gamepad();
    if (pad.connected) {
        const float lx = pad.axes[pad::kAxisLeftX];
        const float ly = pad.axes[pad::kAxisLeftY];
        if (lx * lx + ly * ly > 0.2f * 0.2f) { // outside deadzone
            HUE_LOG_DEBUG("gamepad left stick (%.2f, %.2f)", lx, ly);
        }
    }

    if (input.key_pressed(key::kSpace))
        HUE_LOG_INFO("space pressed");
    if (input.mouse_pressed(mouse::kLeft)) {
        HUE_LOG_INFO("mouse left at (%.0f, %.0f)", input.mouse_x(), input.mouse_y());
    }
}

class AllocationReporter {
public:
    void sample(double frame_seconds) {
        const hue::AllocationSnapshot snapshot = hue::allocation_snapshot();
        for (std::size_t i = 0; i < hue::kMemoryTagCount; ++i) {
            const std::size_t frame_bytes = snapshot.tags[i].frame_requested_bytes;
            std::size_t accumulated = 0;
            if (hue::checked_add(m_total_bytes[i], frame_bytes, accumulated)) {
                m_total_bytes[i] = accumulated;
            } else {
                m_total_bytes[i] = std::numeric_limits<std::size_t>::max();
            }
            if (frame_bytes > m_peak_frame_bytes[i]) {
                m_peak_frame_bytes[i] = frame_bytes;
            }
            m_last_stats[i] = snapshot.tags[i];
        }

        if (m_frame_count != std::numeric_limits<std::size_t>::max()) {
            ++m_frame_count;
        }
        m_elapsed_seconds += frame_seconds;
        if (m_elapsed_seconds >= kReportIntervalSeconds) {
            report("periodic");
        }
    }

    void flush() {
        if (m_frame_count != 0) {
            report("shutdown");
        }
    }

private:
    void report(const char* reason) {
        HUE_LOG_INFO("allocation overlay (%s): %zu frames over %.2fs", reason, m_frame_count,
                     m_elapsed_seconds);
        for (std::size_t i = 0; i < hue::kMemoryTagCount; ++i) {
            const auto tag = static_cast<hue::MemoryTag>(i);
            const std::size_t average = m_total_bytes[i] / m_frame_count;
            HUE_LOG_INFO("memory %-10s avg=%zu B/frame peak=%zu B/frame live=%zu B heap_peak=%zu B",
                         hue::memory_tag_name(tag), average, m_peak_frame_bytes[i],
                         m_last_stats[i].current_bytes, m_last_stats[i].peak_bytes);
            m_total_bytes[i] = 0;
            m_peak_frame_bytes[i] = 0;
        }
        m_frame_count = 0;
        m_elapsed_seconds = 0.0;
    }

    static constexpr double kReportIntervalSeconds = 5.0;
    std::size_t m_total_bytes[hue::kMemoryTagCount] = {};
    std::size_t m_peak_frame_bytes[hue::kMemoryTagCount] = {};
    hue::AllocationStats m_last_stats[hue::kMemoryTagCount] = {};
    std::size_t m_frame_count = 0;
    double m_elapsed_seconds = 0.0;
};

#if defined(HUE_DEBUG_CHANNEL)

// Live loop state exposed through the debug channel. Single-threaded: the
// loop writes these fields and the channel dispatches handlers on the same
// thread inside update().
struct GameDebugContext {
    long long frames = 0;
    long long sim_steps = 0;
    double uptime_seconds = 0.0;
    bool quit_requested = false;
    hue::Renderer* renderer = nullptr; // null when running without a GPU
    Encounter* encounter = nullptr;
    const char* scene_path = nullptr;
    const char* tuning_path = nullptr;
    bool reset_requested = false, start_requested = false;
};

void status_command(void* user_data, const hue::DebugCommandLine& command,
                    hue::DebugResponse& response) {
    (void)command;
    const auto* context = static_cast<const GameDebugContext*>(user_data);
    response.append_format("{\"frames\":%lld,\"sim_steps\":%lld,\"uptime_seconds\":%.3f}",
                           context->frames, context->sim_steps, context->uptime_seconds);
}

void quit_command(void* user_data, const hue::DebugCommandLine& command,
                  hue::DebugResponse& response) {
    (void)command;
    auto* context = static_cast<GameDebugContext*>(user_data);
    context->quit_requested = true;
    response.append("{\"quitting\":true}");
}

void fight_command(void* user_data, const hue::DebugCommandLine& command,
                   hue::DebugResponse& response) {
    auto& c = *static_cast<GameDebugContext*>(user_data);
    if (!c.encounter) {
        response.set_error("encounter unavailable");
        return;
    }
    if (!std::strcmp(command.name, "fight.reset"))
        c.reset_requested = true;
    if (!std::strcmp(command.name, "fight.start"))
        c.start_requested = true;
    const auto& e = *c.encounter;
    response.append_format(
        "{\"state\":\"%s\",\"revision\":%u,\"scene_pending\":%s,\"tuning_pending\":%s,\"player_x\":"
        "%.3f}",
        encounter_state_name(e.state()), e.tuning().revision, e.scene_pending() ? "true" : "false",
        e.tuning_pending() ? "true" : "false", static_cast<double>(e.position(0).x));
}

void config_reload_command(void* user_data, const hue::DebugCommandLine& command,
                           hue::DebugResponse& response) {
    auto& c = *static_cast<GameDebugContext*>(user_data);
    const bool scene = !std::strcmp(command.name, "scene.reload");
    auto bytes = read_config_file(scene ? c.scene_path : c.tuning_path);
    bool accepted = false;
    if (bytes && c.encounter) {
        if (scene) {
            auto parsed = parse_scene(bytes.value().data, bytes.value().size);
            accepted = parsed && c.encounter->stage_scene(parsed.value());
        } else {
            auto parsed = parse_tuning(bytes.value().data, bytes.value().size);
            accepted = parsed && c.encounter->stage_tuning(parsed.value());
        }
    }
    if (accepted)
        response.append("{\"staged\":true,\"applies\":\"next restart\"}");
    else
        response.set_error(
            "invalid or unavailable candidate; active and pending configuration retained");
}

void render_status_command(void* user_data, const hue::DebugCommandLine& command,
                           hue::DebugResponse& response) {
    (void)command;
    const auto* context = static_cast<const GameDebugContext*>(user_data);
    if (context->renderer == nullptr) {
        response.set_error("renderer not active on this machine");
        return;
    }
    const hue::RendererStatus& status = context->renderer->status();
    response.append("{\"adapter\":");
    response.append_json_string(status.adapter);
    response.append_format(",\"swapchain\":[%u,%u],\"frames_rendered\":%llu,"
                           "\"last_draws\":%u,\"last_culled\":%u,\"pipelines\":%u,\"layouts\":%u,"
                           "\"modules\":%u,\"generation\":%llu}",
                           status.swapchain_width, status.swapchain_height,
                           static_cast<unsigned long long>(status.frames_rendered),
                           status.last_draws, status.last_culled, status.live_pipelines,
                           status.live_layouts, status.live_shader_modules,
                           static_cast<unsigned long long>(status.shader_generation));
}

// Runtime recompile hook: recompile .spv externally (build, or glslang by
// hand), then `shader.reload` picks it up without restarting the game.
void shader_reload_command(void* user_data, const hue::DebugCommandLine& command,
                           hue::DebugResponse& response) {
    (void)command;
    auto* context = static_cast<GameDebugContext*>(user_data);
    if (context->renderer == nullptr) {
        response.set_error("renderer not active on this machine");
        return;
    }
    const auto reloaded = context->renderer->reload_shaders();
    if (!reloaded) {
        response.set_error("reload failed; previous pipeline kept (see log)");
        return;
    }
    response.append("{\"reloaded\":true}");
}

#endif // HUE_DEBUG_CHANNEL

// Startup benchmark (Week 3 DoD): the same math-heavy workload run serially
// and through parallel_for, so the log shows the real speedup and a Tracy
// capture shows every hue_worker_N thread busy.
constexpr std::uint32_t kBenchChunk = 4096;
constexpr std::uint32_t kBenchChunkCount = 512;
constexpr std::uint32_t kBenchIterations = kBenchChunk * kBenchChunkCount; // ~2M rotations

struct BenchContext {
    float partial_sums[kBenchChunkCount] = {};
};

void bench_job(void* user_data, std::uint32_t start, std::uint32_t end) {
    auto* context = static_cast<BenchContext*>(user_data);
    const hue::Quat spin = hue::Quat::from_axis_angle({0.0f, 1.0f, 0.0f}, hue::radians(1.0f));
    float sum = 0.0f;
    for (std::uint32_t i = start; i < end; ++i) {
        hue::Vec3 v{static_cast<float>(i & 1023u) * 0.01f, 1.0f, -0.5f};
        v = rotate(spin, v);
        sum += v.x + v.y + v.z;
    }
    // Chunk starts are multiples of kBenchChunk, so this write is exclusive.
    context->partial_sums[start / kBenchChunk] = sum;
}

void run_job_benchmark(hue::JobSystem& jobs) {
    using Clock = std::chrono::steady_clock;
    BenchContext context;

    const auto serial_start = Clock::now();
    for (std::uint32_t begin = 0; begin < kBenchIterations; begin += kBenchChunk) {
        bench_job(&context, begin, begin + kBenchChunk);
    }
    const double serial_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - serial_start).count();

    const auto parallel_start = Clock::now();
    jobs.parallel_for(kBenchIterations, kBenchChunk, &bench_job, &context);
    const double parallel_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - parallel_start).count();

    const double speedup = parallel_ms > 0.0 ? serial_ms / parallel_ms : 0.0;
    HUE_LOG_INFO("job benchmark: %u quat rotations serial %.2f ms, parallel %.2f ms "
                 "(%.2fx on %u workers)",
                 kBenchIterations, serial_ms, parallel_ms, speedup, jobs.worker_count());
}

// Movement tuning shared by the controller and the animation blend, so the
// blend parameter is literally the character's ground speed.
// KayKit bind pose faces +Z (Unity). Hue locomotion treats yaw 0 as -Z,
// matching the generated fixtures and the camera-relative WASD basis.
// Adding π here turns the mesh to match movement without rebaking clips.
constexpr float kModelYawOffset = hue::kPi;

// Opening positions: 1.6 m apart, facing each other, so the handslot hitbox
// can reach the torso hurtbox on the first swing.
constexpr hue::Vec3 kPlayerSpawn{-0.8f, 0.0f, 3.0f};
constexpr hue::Vec3 kEnemySpawn{0.8f, 0.0f, 3.0f};
constexpr float kPlayerFacingDegrees = -90.0f; // +X
constexpr float kEnemyFacingDegrees = 90.0f;   // -X

// ------------------------------------------------------------- ECS pieces

// Week 8 components: enough to route physics -> transforms -> draws through
// the ECS instead of ad-hoc locals. Combat components arrive in Week 9/10.
struct TransformComponent {
    hue::Vec3 position{};
    float yaw = 0.0f; // radians around +Y
};

struct DrawComponent {
    std::uint32_t index = 0; // slot in the frame's MeshDraw array
};

struct BodyComponent {
    std::uint32_t character = 0; // PhysicsWorld character id
};

// Bakes a static render mesh (procedural scene or validated glTF import)
// into one world-space triangle soup and cooks it into a Jolt mesh body.
// Collision therefore comes from the same data the renderer draws - no
// hand-maintained box duplicates.
[[nodiscard]] hue::Result<void> cook_static_collision(const hue::asset::StaticMeshData& data,
                                                      const hue::Mat4& world_transform,
                                                      hue::physics::PhysicsWorld& physics) {
    hue::Array<hue::Vec3> positions{hue::MemoryTag::kPhysics};
    hue::Array<std::uint32_t> indices{hue::MemoryTag::kPhysics};

    for (std::size_t i = 0; i < data.instances.size(); ++i) {
        const hue::asset::MeshInstance& instance = data.instances[i];
        if (instance.primitive_index >= data.primitives.size()) {
            return hue::ErrorCode::kCorruptData;
        }
        const hue::asset::MeshPrimitive& primitive = data.primitives[instance.primitive_index];
        const hue::Mat4 world = world_transform * instance.transform;
        for (std::uint32_t index = 0; index < primitive.index_count; ++index) {
            const std::uint32_t raw = data.indices[primitive.first_index + index];
            const std::int64_t vertex = static_cast<std::int64_t>(raw) + primitive.vertex_offset;
            if (vertex < 0 || vertex >= static_cast<std::int64_t>(data.vertices.size())) {
                return hue::ErrorCode::kCorruptData;
            }
            const hue::Vec3 position =
                world.transform_point(data.vertices[static_cast<std::size_t>(vertex)].position);
            if (!indices.push_back(static_cast<std::uint32_t>(positions.size())) ||
                !positions.push_back(position)) {
                return hue::ErrorCode::kOutOfMemory;
            }
        }
    }
    if (positions.empty()) {
        return hue::ErrorCode::kInvalidArgument;
    }
    return physics.add_static_mesh(positions.data(), positions.size(), indices.data(),
                                   indices.size());
}

// WASD (or left stick) relative to the camera yaw. Keyboard: shift runs.
// Gamepad: stick deflection is analog - direction from the angle, walk-to-
// run speed from the magnitude.
[[nodiscard]] hue::Vec3 movement_input(const hue::Input& input, float camera_yaw,
                                       const GameTuning& tuning) {
    using namespace hue;
    constexpr float kStickDeadzone = 0.2f;

    float forward_axis = 0.0f;
    float right_axis = 0.0f;
    if (input.key_down(key::kW))
        forward_axis += 1.0f;
    if (input.key_down(key::kS))
        forward_axis -= 1.0f;
    if (input.key_down(key::kD))
        right_axis += 1.0f;
    if (input.key_down(key::kA))
        right_axis -= 1.0f;
    float speed = input.key_down(key::kLeftShift) ? tuning.run_speed : tuning.walk_speed;

    const GamepadState& pad = input.gamepad();
    if (pad.connected) {
        const float lx = pad.axes[pad::kAxisLeftX];
        const float ly = pad.axes[pad::kAxisLeftY];
        const float magnitude = std::sqrt(lx * lx + ly * ly);
        if (magnitude > kStickDeadzone) {
            right_axis = lx;
            forward_axis = -ly;
            // Rescale the post-deadzone range so a light tilt walks and a
            // full deflection runs.
            const float deflection =
                clamp((magnitude - kStickDeadzone) / (1.0f - kStickDeadzone), 0.0f, 1.0f);
            speed = lerp(tuning.walk_speed, tuning.run_speed, deflection);
        }
    }
    if (forward_axis == 0.0f && right_axis == 0.0f)
        return {};

    const Vec3 forward{-std::sin(camera_yaw), 0.0f, -std::cos(camera_yaw)};
    const Vec3 right{std::cos(camera_yaw), 0.0f, -std::sin(camera_yaw)};
    const Vec3 direction = forward * forward_axis + right * right_axis;
    return normalize(direction) * speed;
}

void append_hud_quad(hue::HudQuad* quads, std::uint32_t& count, hue::Vec2 min, hue::Vec2 max,
                     hue::Vec4 color) {
    if (count >= hue::kMaxHudQuads) {
        return;
    }
    quads[count++] = {min, max, color};
}

void append_health_bar(hue::HudQuad* quads, std::uint32_t& count, float x0, float y0, float x1,
                       float y1, float ratio, hue::Vec4 fill) {
    ratio = hue::clamp(ratio, 0.0f, 1.0f);
    append_hud_quad(quads, count, {x0, y0}, {x1, y1}, {0.04f, 0.04f, 0.055f, 0.82f});
    const float inset = (y1 - y0) * 0.22f;
    const float inner_x0 = x0 + inset;
    const float inner_x1 = x1 - inset;
    const float inner_y0 = y0 + inset;
    const float inner_y1 = y1 - inset;
    if (ratio > 0.001f && inner_x1 > inner_x0) {
        append_hud_quad(quads, count, {inner_x0, inner_y0},
                        {inner_x0 + (inner_x1 - inner_x0) * ratio, inner_y1}, fill);
    }
}

[[nodiscard]] bool world_to_ndc(const hue::Camera& camera, hue::Vec3 world, hue::Vec2& ndc) {
    const hue::Vec4 clip = camera.view_projection() * hue::Vec4{world.x, world.y, world.z, 1.0f};
    if (!(clip.w > 0.05f) || !std::isfinite(clip.w)) {
        return false;
    }
    ndc.x = clip.x / clip.w;
    ndc.y = clip.y / clip.w;
    return std::isfinite(ndc.x) && std::isfinite(ndc.y);
}

} // namespace

int main(int argc, char** argv) {
    hue::log::init(hue::log::Level::kDebug);
    hue::log::install_crash_handler();
    HUE_LOG_INFO("%s starting", hue::engine_version_string());

    long long max_frames = -1; // run until closed
    bool require_renderer = false;
    bool encounter_smoke = false;
    bool baseline_hud = false; // reproduce Week 12's two-page smoke scenario
    const char* telemetry_path = nullptr;
    char scene_path[1024];
    char tuning_path[1024];
    if (!runtime_path(tuning_path, sizeof(tuning_path), "assets/config/tuning.json"))
        return 1;
    if (!runtime_path(scene_path, sizeof(scene_path), "assets/config/arena.json"))
        return 1;
    bool debug_channel_requested = false;
    [[maybe_unused]] std::uint32_t debug_channel_port = hue::kDebugChannelDefaultPort;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            max_frames = std::atoll(argv[i + 1]);
        } else if (std::strcmp(argv[i], "--telemetry") == 0) {
            if (i + 1 >= argc || !*argv[i + 1]) {
                HUE_LOG_ERROR("--telemetry requires a writable JSONL path");
                return 1;
            }
            telemetry_path = argv[++i];
        } else if (std::strcmp(argv[i], "--baseline-hud") == 0) {
            baseline_hud = true;
        } else if (std::strcmp(argv[i], "--encounter-smoke") == 0) {
            encounter_smoke = true;
            max_frames = 2400;
        } else if (std::strcmp(argv[i], "--require-renderer") == 0) {
            require_renderer = true;
        } else if (std::strcmp(argv[i], "--debug-channel") == 0) {
            debug_channel_requested = true;
            // Optional port argument: --debug-channel 46601
            std::uint32_t parsed_port = 0;
            if (i + 1 < argc && hue::parse_debug_u32(argv[i + 1], parsed_port) &&
                parsed_port <= 65535u) {
                debug_channel_port = parsed_port;
            }
        }
    }

    auto scene_bytes = read_config_file(scene_path);
    if (!scene_bytes) {
        HUE_LOG_ERROR("scene file unavailable: %s", scene_path);
        return 1;
    }
    auto initial_scene = parse_scene(scene_bytes.value().data, scene_bytes.value().size);
    if (!initial_scene) {
        HUE_LOG_ERROR("invalid scene: %s", scene_path);
        return 1;
    }

    auto tuning_bytes = read_config_file(tuning_path);
    if (!tuning_bytes) {
        HUE_LOG_ERROR("tuning file unavailable");
        return 1;
    }
    auto initial_tuning = parse_tuning(tuning_bytes.value().data, tuning_bytes.value().size);
    if (!initial_tuning) {
        HUE_LOG_ERROR("invalid tuning schema or relationships");
        return 1;
    }
    auto platform = hue::Platform::create();
    if (!platform) {
        HUE_LOG_ERROR("platform init failed");
        return 1;
    }

    auto window = hue::Window::create(hue::WindowDesc{});
    if (!window) {
        HUE_LOG_ERROR("window creation failed");
        return 1;
    }
    HUE_LOG_INFO("window open (1280x720)");

    hue::RendererDesc renderer_desc;
#if !defined(NDEBUG)
    renderer_desc.enable_validation = true; // no-op when the layer is absent
#endif
    auto renderer = hue::Renderer::create(window.value(), renderer_desc);
    if (!renderer) {
        if (require_renderer) {
            HUE_LOG_ERROR("--require-renderer set but no usable Vulkan device");
            return 2; // distinct exit code: CI skips instead of failing
        }
        HUE_LOG_WARN("no usable Vulkan device; running without rendering");
    }
    bool renderer_active = renderer.has_value();

    // --- Week 8: physics world; collision cooks from the render meshes ---
    auto physics = hue::physics::PhysicsWorld::create();
    if (!physics) {
        HUE_LOG_ERROR("physics world init failed");
        return 1;
    }

    // Scene mesh data builds regardless of the renderer so headless runs
    // still get a physics arena; GPU uploads stay renderer-gated below.
    const hue::Mat4 cube_world = hue::Mat4::trs(
        {0.0f, 3.75f, 0.0f}, hue::Quat::from_axis_angle({0.0f, 1.0f, 0.0f}, hue::radians(30.0f)),
        {1.5f, 1.5f, 1.5f});
    auto ground = build_ground_scene();
    auto gltf_cube = build_gltf_cube();
    if (!gltf_cube) {
        HUE_LOG_ERROR("glTF cube import failed; scene continues without it");
    }
    if (ground) {
        const auto cooked =
            cook_static_collision(ground.value(), hue::Mat4::identity(), physics.value());
        if (!cooked) {
            HUE_LOG_ERROR("arena collision cook failed");
            return 1;
        }
    }
    if (gltf_cube) {
        const auto cooked = cook_static_collision(gltf_cube.value(), cube_world, physics.value());
        if (!cooked) {
            HUE_LOG_ERROR("glTF cube collision cook failed");
            return 1;
        }
    }

    // Load gameplay assets even without Vulkan: headless sessions exercise the
    // same combat/animation path. Missing characters are fatal, never an empty arena.
    hue::MeshDraw draws[4];
    std::optional<CombatCharacter> combat_characters[2];
    std::uint32_t character_draw_indices[2]{};
    std::uint32_t draw_count = 0;
    if (renderer_active) {
        if (ground) {
            auto uploaded = renderer.value().upload_static_mesh(ground.value());
            if (!uploaded)
                return 1;
            draws[draw_count++].mesh = uploaded.value();
        }
        if (gltf_cube) {
            auto uploaded = renderer.value().upload_static_mesh(gltf_cube.value());
            if (!uploaded)
                return 1;
            draws[draw_count].mesh = uploaded.value();
            draws[draw_count++].transform = cube_world;
        }
    }
    const char* model_files[2] = {"player.glb", "enemy.glb"};
    const char* event_files[2] = {"player.events.json", "enemy.events.json"};
    for (std::uint32_t i = 0; i < 2; ++i) {
        auto skinned = load_character(model_files[i]);
        auto events = load_character_events(event_files[i]);
        if (!skinned || !events) {
            HUE_LOG_ERROR("required encounter assets are missing or invalid");
            return 1;
        }
        hue::MeshHandle mesh{};
        if (renderer_active) {
            auto uploaded = renderer.value().upload_skinned_mesh(skinned.value());
            if (!uploaded)
                return 1;
            mesh = uploaded.value();
        }
        character_draw_indices[i] = draw_count;
        combat_characters[i].emplace(std::move(skinned.value()), std::move(events.value()), mesh,
                                     draw_count++);
        if (!combat_characters[i]->initialize())
            return 1;
    }
    if (renderer_active) {
        const hue::PointLight lights[2] = {
            {{0.0f, 4.2f, 0.0f}, 10.0f, {1.0f, 0.55f, 0.25f}, 14.0f},
            {{0.0f, 1.8f, 4.5f}, 8.0f, {0.35f, 0.5f, 1.0f}, 8.0f},
        };
        renderer.value().set_point_lights(lights, 2);
    }
    FlyCamera fly_camera;
    FollowCamera follow_camera;
    bool use_fly_camera = false;

    // --- Week 8: capsule controllers + ECS world --------------------------

    hue::physics::CharacterDesc player_capsule;
    player_capsule.position = {kPlayerSpawn.x, 0.05f, kPlayerSpawn.z};
    const auto player_body = physics.value().create_character(player_capsule);
    if (!player_body) {
        HUE_LOG_ERROR("player capsule controller creation failed");
        return 1;
    }
    // The enemy stands on its own capsule (grounded by the same solver).
    // Week 11 AI writes its desired planar velocity here each tick.
    hue::physics::CharacterDesc enemy_capsule;
    enemy_capsule.position = {kEnemySpawn.x, 0.05f, kEnemySpawn.z};
    const auto enemy_body = physics.value().create_character(enemy_capsule);
    if (!enemy_body) {
        HUE_LOG_ERROR("enemy capsule controller creation failed");
        return 1;
    }
    HUE_LOG_INFO("physics arena: %u static bodies (cooked from render meshes), "
                 "%u capsule controllers",
                 physics.value().static_body_count(), physics.value().character_count());

    hue::ecs::World world;
    auto transform_pool = world.pool<TransformComponent>();
    auto draw_pool = world.pool<DrawComponent>();
    auto body_pool = world.pool<BodyComponent>();
    if (!transform_pool || !draw_pool || !body_pool) {
        HUE_LOG_ERROR("ecs pool init failed");
        return 1;
    }
    hue::ecs::Entity player_entity{};
    hue::ecs::Entity enemy_entity{};
    {
        const auto created_player = world.create();
        const auto created_enemy = world.create();
        if (!created_player || !created_enemy) {
            HUE_LOG_ERROR("ecs entity creation failed");
            return 1;
        }
        player_entity = created_player.value();
        enemy_entity = created_enemy.value();
        bool ok = world
                      .add(player_entity,
                           TransformComponent{kPlayerSpawn, hue::radians(kPlayerFacingDegrees)})
                      .has_value();
        ok = ok && world.add(player_entity, BodyComponent{player_body.value()}).has_value();
        ok = ok && world
                       .add(enemy_entity,
                            TransformComponent{kEnemySpawn, hue::radians(kEnemyFacingDegrees)})
                       .has_value();
        ok = ok && world.add(enemy_entity, BodyComponent{enemy_body.value()}).has_value();
        if (ok && character_draw_indices[0] != UINT32_MAX) {
            ok = world.add(player_entity, DrawComponent{character_draw_indices[0]}).has_value();
        }
        if (ok && character_draw_indices[1] != UINT32_MAX) {
            ok = world.add(enemy_entity, DrawComponent{character_draw_indices[1]}).has_value();
        }
        if (!ok) {
            HUE_LOG_ERROR("ecs component setup failed");
            return 1;
        }
    }
    HUE_LOG_INFO("ecs world: %zu entities", world.alive_count());

    auto jobs = hue::JobSystem::create();
    if (!jobs) {
        HUE_LOG_ERROR("job system init failed");
        return 1;
    }
    HUE_LOG_INFO("job system: %u workers", jobs.value().worker_count());
    run_job_benchmark(jobs.value());

    auto animation_arena = hue::LinearArena::create(256 * 1024, hue::MemoryTag::kAnimation);
    if (!animation_arena) {
        HUE_LOG_ERROR("animation frame arena init failed");
        return 1;
    }

#if defined(HUE_DEBUG_CHANNEL)
    GameDebugContext debug_context;
    debug_context.renderer = renderer_active ? &renderer.value() : nullptr;
    auto debug_channel =
        debug_channel_requested
            ? hue::DebugChannel::create(static_cast<std::uint16_t>(debug_channel_port))
            : hue::Result<hue::DebugChannel>(hue::ErrorCode::kUnsupported);
    if (debug_channel_requested) {
        if (!debug_channel) {
            HUE_LOG_ERROR("debug channel init failed");
            return 1;
        }
        auto registered = debug_channel.value().register_command(
            "status", "frames, sim steps, and uptime of the running game", &status_command,
            &debug_context);
        if (registered) {
            registered = debug_channel.value().register_command("quit", "request a clean shutdown",
                                                                &quit_command, &debug_context);
        }
        if (registered) {
            registered = debug_channel.value().register_command(
                "render.status", "adapter, swapchain size, frames rendered", &render_status_command,
                &debug_context);
        }
        if (registered) {
            registered = debug_channel.value().register_command(
                "shader.reload", "reload .spv shaders and rebuild the pipeline",
                &shader_reload_command, &debug_context);
        }
        for (const char* name : {"fight.status", "fight.start", "fight.reset"}) {
            if (registered)
                registered = debug_channel.value().register_command(
                    name, "inspect or queue encounter lifecycle action", &fight_command,
                    &debug_context);
        }
        for (const char* name : {"scene.reload", "tuning.reload"}) {
            if (registered)
                registered = debug_channel.value().register_command(
                    name, "validate config; apply on next restart", &config_reload_command,
                    &debug_context);
        }
        if (!registered) {
            HUE_LOG_ERROR("debug channel command registration failed");
            return 1;
        }
    }
#else
    if (debug_channel_requested) {
        HUE_LOG_WARN("--debug-channel ignored: HUE_DEBUG_CHANNEL not compiled in");
    }
#endif

    hue::Input input;
    hue::FrameClock clock;
    hue::FixedTimestep timestep;
    AllocationReporter allocation_reporter;
    bool gamepad_was_connected = false;
    long long frames = 0;

    Encounter encounter(physics.value(), *combat_characters[0], *combat_characters[1],
                        player_body.value(), enemy_body.value(), follow_camera);
    if (!encounter.stage_scene(initial_scene.value()))
        return 1;
    if (!encounter.stage_tuning(initial_tuning.value())) {
        HUE_LOG_ERROR("tuning conflicts with clip events");
        return 1;
    }
    if (!encounter.reset(animation_arena.value(), draws))
        return 1;
    unsigned diagnostics = 0;
    FightRecorder fight_recorder;
    const auto telemetry_source =
        encounter_smoke ? FightSource::kInjected : FightSource::kInteractive;
    const auto monotonic_seconds = [] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    };
    const char* reload_status = "F5 SCENE / F6 TUNING. VALID EDITS APPLY ON RESTART.";
#if defined(HUE_DEBUG_CHANNEL)
    debug_context.encounter = &encounter;
    debug_context.scene_path = scene_path;
    debug_context.tuning_path = tuning_path;
#endif
    double frame_samples[4096]{};
    std::size_t frame_sample_count = 0;
    std::size_t frame_sample_cursor = 0;
    HUE_LOG_INFO("Enter/pad-Start begin; R restart; F2/pad-Back diagnostics");
    while (!window.value().should_close()) {
        HUE_PROFILE_ZONE("game::frame");
        hue::memory_begin_frame();
        animation_arena.value().reset();
        window.value().poll_events();
        input.update(window.value());

        if (input.gamepad().connected != gamepad_was_connected) {
            gamepad_was_connected = input.gamepad().connected;
            if (gamepad_was_connected) {
                HUE_LOG_INFO("gamepad connected: %s", input.gamepad().name);
            } else {
                HUE_LOG_INFO("gamepad disconnected");
            }
        }
        if (input.key_pressed(hue::key::kEscape))
            break;
        if (input.key_pressed(hue::key::kF1)) {
            use_fly_camera = !use_fly_camera;
            HUE_LOG_INFO("camera: %s", use_fly_camera ? "debug fly" : "third-person follow");
        }

        const double frame_seconds = clock.tick();
        const int steps =
            timestep.advance(encounter_smoke ? hue::FixedTimestep::kTickSeconds : frame_seconds);
        frame_samples[frame_sample_cursor++ % 4096] = frame_seconds * 1000.0;
        if (frame_sample_count < 4096)
            ++frame_sample_count;
        if (input.key_pressed(hue::key::kF2) || input.pad_pressed(hue::pad::kBack))
            diagnostics = (diagnostics + 1) % 3;
        if (input.key_pressed(hue::key::kF5)) {
            reload_status = "SCENE REJECTED - LAST WORKING DATA KEPT";
            auto bytes = read_config_file(scene_path);
            if (bytes) {
                auto candidate = parse_scene(bytes.value().data, bytes.value().size);
                if (candidate && encounter.stage_scene(candidate.value())) {
                    reload_status = "SCENE STAGED - R TO APPLY";
                    HUE_LOG_INFO("scene accepted; applies on next restart");
                } else
                    HUE_LOG_WARN("scene rejected; current and pending scene unchanged");
            } else
                HUE_LOG_WARN("scene read failed; current and pending scene unchanged");
        }

        if (input.key_pressed(hue::key::kF6)) {
            reload_status = "TUNING REJECTED - LAST WORKING DATA KEPT";
            auto bytes = read_config_file(tuning_path);
            if (bytes) {
                auto candidate = parse_tuning(bytes.value().data, bytes.value().size);
                if (candidate && encounter.stage_tuning(candidate.value()))
                    reload_status = "TUNING STAGED - R TO APPLY";
            }
            HUE_LOG_INFO("%s", reload_status);
        }
        bool begin = input.key_pressed(hue::key::kEnter) || input.pad_pressed(hue::pad::kStart);
        bool resetting = input.key_pressed(hue::key::kR) ||
                         (begin && encounter.state() != EncounterState::kReady &&
                          encounter.state() != EncounterState::kFighting);
#if defined(HUE_DEBUG_CHANNEL)
        begin = begin || debug_context.start_requested;
        resetting = resetting || debug_context.reset_requested;
        debug_context.start_requested = debug_context.reset_requested = false;
#endif
        if (encounter_smoke)
            resetting = frames % 120 == 0;
        if (resetting) {
            // Abandoned fights are not completed-fight evidence.
            fight_recorder = FightRecorder{};
            if (!world.flush() || !encounter.reset(animation_arena.value(), draws))
                return 1;
            timestep.reset_accumulator();
            reload_status = "ACTIVE CONFIGURATION APPLIED";
        } else if (begin) {
            encounter.start();
        }
        if (encounter_smoke) {
            if (frames % 120 == 1) {
                encounter.start();
                encounter.toggle_lock();
            }
            if (frames % 120 == 5)
                encounter.buffer(hue::combat::Action::kAttackHeavy);
            if (frames % 120 == 60)
                combat_characters[(frames / 120) % 2 == 0 ? 1 : 0]->apply_damage(1000.0f);
            diagnostics = static_cast<unsigned>((frames / 120) % (baseline_hud ? 2 : 3));
        }

        // Combat input: presses buffer into the state machine, which
        // consumes them at the next legal moment (cancel window/state end).
        {
            using hue::combat::Action;
            Action pressed = Action::kNone;
            if (input.mouse_pressed(hue::mouse::kLeft) || input.pad_pressed(hue::pad::kX)) {
                pressed = Action::kAttackLight;
            }
            if (input.key_pressed(hue::key::kE) || input.pad_pressed(hue::pad::kY)) {
                pressed = Action::kAttackHeavy;
            }
            if (input.key_pressed(hue::key::kSpace) || input.pad_pressed(hue::pad::kB)) {
                pressed = Action::kDodge;
            }
            if (!resetting)
                encounter.buffer(pressed);
        }

        if (!resetting &&
            (input.key_pressed(hue::key::kTab) || input.pad_pressed(hue::pad::kRightThumb)))
            encounter.toggle_lock();
        (void)fight_recorder.observe(encounter, monotonic_seconds(), telemetry_source);
        for (int step = 0; step < steps; ++step) {
            if (step == 0)
                log_input_edges(input);
            auto advanced =
                encounter.tick(movement_input(input, follow_camera.yaw(), encounter.tuning()),
                               animation_arena.value(), draws);
            if (!advanced) {
                HUE_LOG_ERROR("encounter tick failed");
                return 1;
            }
        }
        if (auto report =
                fight_recorder.observe(encounter, monotonic_seconds(), telemetry_source)) {
            if (telemetry_path && !append_fight_report(telemetry_path, *report)) {
                HUE_LOG_WARN(
                    "telemetry write failed; disabled for this session (gameplay continues)");
                telemetry_path = nullptr;
            }
        }
        if (!world.flush())
            return 1;
        const hue::ecs::Entity actors[2] = {player_entity, enemy_entity};
        for (std::uint32_t i = 0; i < 2; ++i) {
            auto* transform = transform_pool.value()->get(actors[i]);
            if (transform == nullptr)
                return 1;
            transform->position = encounter.position(i);
            transform->yaw = encounter.yaw(i);
        }
        if (encounter_smoke && frames % 120 == 119) {
            const auto expected =
                (frames / 120) % 2 == 0 ? EncounterState::kWon : EncounterState::kLost;
            if (encounter.state() != expected || encounter.locked() ||
                physics.value().character_count() != 2 || world.alive_count() != 2) {
                HUE_LOG_ERROR("encounter smoke failed at cycle %lld", frames / 120);
                return 1;
            }
        }

        if (renderer_active) {
            // ECS transforms -> draw list.
            hue::ecs::for_each(
                *draw_pool.value(), *transform_pool.value(),
                [&](hue::ecs::Entity, DrawComponent& draw, TransformComponent& transform) {
                    draws[draw.index].transform =
                        hue::Mat4::trs(transform.position,
                                       hue::Quat::from_axis_angle({0.0f, 1.0f, 0.0f},
                                                                  transform.yaw + kModelYawOffset),
                                       {1.0f, 1.0f, 1.0f});
                });

            if (use_fly_camera) {
                fly_camera.update(input, static_cast<float>(frame_seconds));
            } else {
                const hue::Vec3 lock_point =
                    physics.value().character_position(enemy_body.value()) +
                    hue::Vec3{0.0f, 1.2f, 0.0f}; // chest height frames better than feet
                follow_camera.update(input, static_cast<float>(frame_seconds),
                                     physics.value().character_position(player_body.value()),
                                     physics.value(), encounter.locked() ? &lock_point : nullptr);
            }
            const hue::RendererStatus& render_status = renderer.value().status();
            const float aspect = render_status.swapchain_height > 0
                                     ? static_cast<float>(render_status.swapchain_width) /
                                           static_cast<float>(render_status.swapchain_height)
                                     : 16.0f / 9.0f;
            const hue::Camera camera =
                use_fly_camera ? fly_camera.camera(aspect) : follow_camera.camera(aspect);

            static hue::HudQuad hud[hue::kMaxHudQuads];
            std::uint32_t hud_count = 0;
            const float ratio_player =
                combat_characters[0] ? combat_characters[0]->health_ratio() : 0.0f;
            const float ratio_enemy =
                combat_characters[1] ? combat_characters[1]->health_ratio() : 0.0f;
            const hue::Vec4 fill_player{0.28f, 0.86f, 0.48f, 0.94f};
            const hue::Vec4 fill_enemy{0.90f, 0.26f, 0.32f, 0.94f};
            append_health_bar(hud, hud_count, -0.92f, 0.84f, -0.10f, 0.94f, ratio_player,
                              fill_player);
            append_health_bar(hud, hud_count, 0.10f, 0.84f, 0.92f, 0.94f, ratio_enemy, fill_enemy);

            const hue::RendererStatus& hud_status = render_status;
            const float pixel_x = hud_status.swapchain_width > 0
                                      ? 2.0f / static_cast<float>(hud_status.swapchain_width)
                                      : 2.0f / 1280.0f;
            const float pixel_y = hud_status.swapchain_height > 0
                                      ? 2.0f / static_cast<float>(hud_status.swapchain_height)
                                      : 2.0f / 720.0f;
            const struct {
                std::uint32_t body;
                float ratio;
                hue::Vec4 fill;
            } overhead[2] = {
                {player_body.value(), ratio_player, fill_player},
                {enemy_body.value(), ratio_enemy, fill_enemy},
            };
            for (const auto& bar : overhead) {
                hue::Vec2 ndc{};
                const hue::Vec3 head =
                    physics.value().character_position(bar.body) + hue::Vec3{0.0f, 1.92f, 0.0f};
                if (!world_to_ndc(camera, head, ndc)) {
                    continue;
                }
                const float half_w = 60.0f * pixel_x;
                const float height = 14.0f * pixel_y;
                append_health_bar(hud, hud_count, ndc.x - half_w, ndc.y + 10.0f * pixel_y,
                                  ndc.x + half_w, ndc.y + 10.0f * pixel_y + height, bar.ratio,
                                  bar.fill);
            }

            append_combat_hud(hud, hud_count, camera, encounter, *combat_characters[0],
                              *combat_characters[1], diagnostics, frame_seconds * 1000.0,
                              animation_arena.value().high_water_mark(), world.alive_count(),
                              physics.value().character_count(), reload_status,
                              hud_status.swapchain_width, hud_status.swapchain_height);
            const auto drawn =
                renderer.value().draw_frame(camera, draws, draw_count, hud, hud_count);
            if (!drawn) {
                HUE_LOG_ERROR("draw_frame failed; rendering disabled for this run");
                if (require_renderer || encounter_smoke)
                    return 1;
                renderer_active = false;
#if defined(HUE_DEBUG_CHANNEL)
                debug_context.renderer = nullptr;
#endif
            }
        }

        ++frames;
        allocation_reporter.sample(frame_seconds);

#if defined(HUE_DEBUG_CHANNEL)
        if (debug_channel) {
            debug_context.frames = frames;
            debug_context.sim_steps = timestep.total_steps();
            debug_context.uptime_seconds += frame_seconds;
            debug_channel.value().update();
            if (debug_context.quit_requested) {
                HUE_LOG_INFO("quit requested via debug channel");
                break;
            }
        }
#endif

        HUE_PROFILE_FRAME();
        if (max_frames >= 0 && frames >= max_frames)
            break;
    }

    if (frame_sample_count > 0) {
        std::sort(frame_samples, frame_samples + frame_sample_count);
        HUE_LOG_INFO("performance_summary samples=%zu p50_ms=%.3f p95_ms=%.3f pose_peak=%zu",
                     frame_sample_count, frame_samples[frame_sample_count / 2],
                     frame_samples[(frame_sample_count - 1) * 95 / 100],
                     animation_arena.value().high_water_mark());
    }
    if (encounter_smoke) {
        if (frames != 2400)
            return 1;
        HUE_LOG_INFO("encounter_smoke PASS: 20 resets, alternating outcomes, diagnostics on/off");
    }
    allocation_reporter.flush();
    HUE_LOG_INFO("shutdown after %lld frames, %lld sim steps", frames, timestep.total_steps());
    return 0;
}
