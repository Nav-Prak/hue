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

#include "characters.h"
#include "fly_camera.h"
#include "follow_camera.h"
#include "test_meshes.h"

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
#include "hue/anim/animation.h"
#include "hue/ecs/ecs.h"
#include "hue/physics/physics.h"
#include "hue/render/camera.h"
#include "hue/render/renderer.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
                           "\"last_draws\":%u,\"last_culled\":%u}",
                           status.swapchain_width, status.swapchain_height,
                           static_cast<unsigned long long>(status.frames_rendered),
                           status.last_draws, status.last_culled);
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
constexpr float kWalkSpeed = 1.6f;  // m/s, matches the walk gait
constexpr float kRunSpeed = 4.0f;   // m/s, matches the locomotion (run) gait
constexpr float kIdleThreshold = 0.2f;

class AnimatedCharacter {
public:
    AnimatedCharacter(hue::asset::SkinnedMeshData&& data,
                      hue::anim::AnimationEventTrack&& events, hue::MeshHandle mesh,
                      std::uint32_t draw_index, bool auto_attack, float first_attack_delay)
        : m_data(std::move(data)), m_events(std::move(events)), m_mesh(mesh),
          m_draw_index(draw_index), m_attack_countdown(first_attack_delay),
          m_auto_attack(auto_attack) {}

    AnimatedCharacter(const AnimatedCharacter&) = delete;
    AnimatedCharacter& operator=(const AnimatedCharacter&) = delete;

    [[nodiscard]] hue::Result<void> initialize() {
        const auto valid = hue::anim::validate_animation_events(m_data, m_events);
        if (!valid) return valid.error();
        const std::int32_t run = hue::anim::find_clip(m_data, "locomotion");
        const std::int32_t walk = hue::anim::find_clip(m_data, "walk");
        const std::int32_t idle = hue::anim::find_clip(m_data, "idle");
        const std::int32_t attack = hue::anim::find_clip(m_data, "attack");
        if (run < 0 || walk < 0 || idle < 0 || attack < 0) return hue::ErrorCode::kCorruptData;
        m_run_clip = static_cast<std::uint32_t>(run);
        m_walk_clip = static_cast<std::uint32_t>(walk);
        m_idle_clip = static_cast<std::uint32_t>(idle);
        m_attack_clip = static_cast<std::uint32_t>(attack);
        return m_animator.bind(m_data, m_idle_clip);
    }

    void request_attack() noexcept { m_attack_requested = true; }

    // Ground speed for the 1D locomotion blend (walk at 1.6, run at 4.0).
    void set_locomotion_speed(float speed) noexcept { m_speed = speed; }

    [[nodiscard]] bool attacking() const noexcept {
        return !m_animator.blending() && m_animator.current_clip() == m_attack_clip &&
               !m_animator.finished();
    }

    [[nodiscard]] hue::Result<void> update(float delta_seconds, hue::LinearArena& arena,
                                           hue::MeshDraw* draws) {
        m_attack_countdown -= delta_seconds;
        if ((m_attack_requested || (m_auto_attack && m_attack_countdown <= 0.0f)) &&
            !attacking()) {
            const auto played = m_animator.play(m_attack_clip, 0.12f, false);
            if (!played) return played.error();
            m_attack_requested = false;
            m_attack_countdown = 2.5f;
        }

        if (!attacking()) {
            // Locomotion layer: idle when standing, walk/run 1D blend by
            // actual ground speed while moving.
            if (m_speed < kIdleThreshold) {
                const auto played = m_animator.play(m_idle_clip, 0.2f, true);
                if (!played) return played.error();
            } else {
                const auto played =
                    m_animator.play_blend(m_walk_clip, m_run_clip, kWalkSpeed, kRunSpeed, 0.15f);
                if (!played) return played.error();
                m_animator.set_blend_parameter(m_speed);
            }
        }

        const hue::anim::AnimationEvent* fired[16]{};
        auto frame = m_animator.update(delta_seconds, arena, &m_events, fired, 16);
        if (!frame) return frame.error();
        for (std::uint32_t i = 0; i < frame.value().fired_event_count; ++i) {
            apply_event(*fired[i]);
        }
        draws[m_draw_index].mesh = m_mesh;
        draws[m_draw_index].joint_matrices = frame.value().skinning_matrices;
        draws[m_draw_index].joint_count = frame.value().joint_count;
        return {};
    }

private:
    void apply_event(const hue::anim::AnimationEvent& event) noexcept {
        if (std::strcmp(event.name, "hit_begin") == 0) m_hitbox_active = true;
        else if (std::strcmp(event.name, "hit_end") == 0) m_hitbox_active = false;
        else if (std::strcmp(event.name, "cancel_open") == 0) m_cancel_open = true;
        else if (std::strcmp(event.name, "cancel_close") == 0) m_cancel_open = false;
        else if (std::strcmp(event.name, "iframe_begin") == 0) m_invulnerable = true;
        else if (std::strcmp(event.name, "iframe_end") == 0) m_invulnerable = false;
        HUE_LOG_DEBUG("anim event %s/%.3f: %s (hitbox=%d cancel=%d iframe=%d)", event.clip,
                      static_cast<double>(event.time), event.name,
                      m_hitbox_active ? 1 : 0, m_cancel_open ? 1 : 0,
                      m_invulnerable ? 1 : 0);
    }

    hue::asset::SkinnedMeshData m_data;
    hue::anim::AnimationEventTrack m_events;
    hue::anim::Animator m_animator;
    hue::MeshHandle m_mesh;
    std::uint32_t m_draw_index = 0;
    std::uint32_t m_run_clip = 0;
    std::uint32_t m_walk_clip = 0;
    std::uint32_t m_idle_clip = 0;
    std::uint32_t m_attack_clip = 0;
    float m_attack_countdown = 0.0f;
    float m_speed = 0.0f;
    bool m_auto_attack = false;
    bool m_attack_requested = false;
    bool m_hitbox_active = false;
    bool m_cancel_open = true;
    bool m_invulnerable = false;
};

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
[[nodiscard]] hue::Result<void>
cook_static_collision(const hue::asset::StaticMeshData& data, const hue::Mat4& world_transform,
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
            const std::int64_t vertex =
                static_cast<std::int64_t>(raw) + primitive.vertex_offset;
            if (vertex < 0 || vertex >= static_cast<std::int64_t>(data.vertices.size())) {
                return hue::ErrorCode::kCorruptData;
            }
            const hue::Vec3 position = world.transform_point(
                data.vertices[static_cast<std::size_t>(vertex)].position);
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

[[nodiscard]] float wrap_angle(float radians_in) noexcept {
    while (radians_in > hue::kPi) radians_in -= hue::kTwoPi;
    while (radians_in < -hue::kPi) radians_in += hue::kTwoPi;
    return radians_in;
}

// WASD (or left stick) relative to the camera yaw. Keyboard: shift runs.
// Gamepad: stick deflection is analog - direction from the angle, walk-to-
// run speed from the magnitude.
[[nodiscard]] hue::Vec3 movement_input(const hue::Input& input, float camera_yaw) {
    using namespace hue;
    constexpr float kStickDeadzone = 0.2f;

    float forward_axis = 0.0f;
    float right_axis = 0.0f;
    if (input.key_down(key::kW)) forward_axis += 1.0f;
    if (input.key_down(key::kS)) forward_axis -= 1.0f;
    if (input.key_down(key::kD)) right_axis += 1.0f;
    if (input.key_down(key::kA)) right_axis -= 1.0f;
    float speed = input.key_down(key::kLeftShift) ? kRunSpeed : kWalkSpeed;

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
            speed = lerp(kWalkSpeed, kRunSpeed, deflection);
        }
    }
    if (forward_axis == 0.0f && right_axis == 0.0f) return {};

    const Vec3 forward{-std::sin(camera_yaw), 0.0f, -std::cos(camera_yaw)};
    const Vec3 right{std::cos(camera_yaw), 0.0f, -std::sin(camera_yaw)};
    const Vec3 direction = forward * forward_axis + right * right_axis;
    return normalize(direction) * speed;
}

} // namespace

int main(int argc, char** argv) {
    hue::log::init(hue::log::Level::kDebug);
    hue::log::install_crash_handler();
    HUE_LOG_INFO("%s starting", hue::engine_version_string());

    long long max_frames = -1; // run until closed
    bool require_renderer = false;
    bool debug_channel_requested = false;
    std::uint32_t debug_channel_port = hue::kDebugChannelDefaultPort;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            max_frames = std::atoll(argv[i + 1]);
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
        {0.0f, 3.75f, 0.0f},
        hue::Quat::from_axis_angle({0.0f, 1.0f, 0.0f}, hue::radians(30.0f)),
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
        const auto cooked =
            cook_static_collision(gltf_cube.value(), cube_world, physics.value());
        if (!cooked) {
            HUE_LOG_ERROR("glTF cube collision cook failed");
            return 1;
        }
    }

    // Renderer/assets scene plus two live Week 7 GPU-skinned characters.
    hue::MeshDraw draws[4];
    std::optional<AnimatedCharacter> animated_characters[2];
    std::uint32_t character_draw_indices[2] = {UINT32_MAX, UINT32_MAX};
    std::uint32_t draw_count = 0;
    if (renderer_active) {
        if (ground) {
            const auto uploaded = renderer.value().upload_static_mesh(ground.value());
            if (uploaded) {
                draws[draw_count].mesh = uploaded.value();
                draws[draw_count].transform = hue::Mat4::identity();
                ++draw_count;
            }
        }
        if (gltf_cube) {
            const auto uploaded = renderer.value().upload_static_mesh(gltf_cube.value());
            if (uploaded) {
                draws[draw_count].mesh = uploaded.value();
                draws[draw_count].transform = cube_world;
                ++draw_count;
            }
        }

        const struct {
            const char* file;
            const char* events;
            hue::Vec3 position;
            float facing_degrees;
            bool auto_attack;
            float first_attack_delay;
        } characters[2] = {
            {"player.glb", "player.events.json", {-1.5f, 0.0f, 3.0f}, 160.0f, false, 0.0f},
            {"enemy.glb", "enemy.events.json", {1.5f, 0.0f, 3.0f}, 200.0f, true, 2.0f},
        };
        for (std::uint32_t character_index = 0; character_index < 2; ++character_index) {
            const auto& character = characters[character_index];
            auto skinned = load_character(character.file);
            if (!skinned) {
                continue; // already logged; scene continues without them
            }
            auto events = load_character_events(character.events);
            if (!events) {
                continue;
            }
            const auto valid_events =
                hue::anim::validate_animation_events(skinned.value(), events.value());
            if (!valid_events) {
                HUE_LOG_WARN("animation events do not match %s", character.file);
                continue;
            }
            const auto uploaded = renderer.value().upload_skinned_mesh(skinned.value());
            if (uploaded) {
                const std::uint32_t character_draw = draw_count;
                draws[character_draw].mesh = uploaded.value();
                draws[draw_count].transform = hue::Mat4::trs(
                    character.position,
                    hue::Quat::from_axis_angle({0.0f, 1.0f, 0.0f},
                                               hue::radians(character.facing_degrees)),
                    {1.0f, 1.0f, 1.0f});
                ++draw_count;
                character_draw_indices[character_index] = character_draw;
                animated_characters[character_index].emplace(
                    std::move(skinned.value()), std::move(events.value()), uploaded.value(),
                    character_draw, character.auto_attack, character.first_attack_delay);
                const auto initialized = animated_characters[character_index]->initialize();
                if (!initialized) {
                    HUE_LOG_ERROR("animation runtime init failed for %s", character.file);
                    return 1;
                }
            }
        }

        // Point lights (Week 6 spec: one directional + point lights): a
        // warm torch over the pillar and a cool fill between the fighters.
        const hue::PointLight point_lights[2] = {
            {{0.0f, 4.2f, 0.0f}, 10.0f, {1.0f, 0.55f, 0.25f}, 14.0f},
            {{0.0f, 1.8f, 4.5f}, 8.0f, {0.35f, 0.5f, 1.0f}, 8.0f},
        };
        renderer.value().set_point_lights(point_lights, 2);

        if (draw_count > 0) {
            HUE_LOG_INFO("scene ready: %u meshes, 2 point lights (RMB orbit, WASD move, "
                         "shift run, Space/pad-X attack, F1 debug fly cam)",
                         draw_count);
        }
    }
    FlyCamera fly_camera;
    FollowCamera follow_camera;
    bool use_fly_camera = false;

    // --- Week 8: capsule controllers + ECS world --------------------------

    hue::physics::CharacterDesc player_capsule;
    player_capsule.position = {-1.5f, 0.05f, 3.0f};
    const auto player_body = physics.value().create_character(player_capsule);
    if (!player_body) {
        HUE_LOG_ERROR("player capsule controller creation failed");
        return 1;
    }
    // The enemy stands on its own capsule (grounded by the same solver);
    // it only starts steering itself when AI lands in Week 11.
    hue::physics::CharacterDesc enemy_capsule;
    enemy_capsule.position = {1.5f, 0.05f, 3.0f};
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
    {
        const auto player_entity = world.create();
        const auto enemy_entity = world.create();
        if (!player_entity || !enemy_entity) {
            HUE_LOG_ERROR("ecs entity creation failed");
            return 1;
        }
        bool ok =
            world.add(player_entity.value(),
                      TransformComponent{{-1.5f, 0.0f, 3.0f}, hue::radians(160.0f)})
                .has_value();
        ok = ok && world.add(player_entity.value(), BodyComponent{player_body.value()})
                       .has_value();
        ok = ok && world.add(enemy_entity.value(),
                             TransformComponent{{1.5f, 0.0f, 3.0f}, hue::radians(200.0f)})
                       .has_value();
        ok = ok && world.add(enemy_entity.value(), BodyComponent{enemy_body.value()})
                       .has_value();
        if (ok && character_draw_indices[0] != UINT32_MAX) {
            ok = world.add(player_entity.value(), DrawComponent{character_draw_indices[0]})
                     .has_value();
        }
        if (ok && character_draw_indices[1] != UINT32_MAX) {
            ok = world.add(enemy_entity.value(), DrawComponent{character_draw_indices[1]})
                     .has_value();
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

    auto animation_arena =
        hue::LinearArena::create(256 * 1024, hue::MemoryTag::kAnimation);
    if (!animation_arena) {
        HUE_LOG_ERROR("animation frame arena init failed");
        return 1;
    }

#if defined(HUE_DEBUG_CHANNEL)
    GameDebugContext debug_context;
    debug_context.renderer = renderer_active ? &renderer.value() : nullptr;
    auto debug_channel = debug_channel_requested
                             ? hue::DebugChannel::create(
                                   static_cast<std::uint16_t>(debug_channel_port))
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
            registered = debug_channel.value().register_command(
                "quit", "request a clean shutdown", &quit_command, &debug_context);
        }
        if (registered) {
            registered = debug_channel.value().register_command(
                "render.status", "adapter, swapchain size, frames rendered",
                &render_status_command, &debug_context);
        }
        if (registered) {
            registered = debug_channel.value().register_command(
                "shader.reload", "reload .spv shaders and rebuild the pipeline",
                &shader_reload_command, &debug_context);
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
        const int steps = timestep.advance(frame_seconds);
        if ((input.key_pressed(hue::key::kSpace) || input.pad_pressed(hue::pad::kX)) &&
            animated_characters[0]) {
            animated_characters[0]->request_attack();
        }
        for (int s = 0; s < steps; ++s) {
            constexpr float kTick = static_cast<float>(hue::FixedTimestep::kTickSeconds);
            log_input_edges(input); // input edges logged per sim tick

            // Player movement: camera-relative WASD/stick through the
            // capsule controller; the solver output drives the anim blend.
            const hue::Vec3 desired = movement_input(input, follow_camera.yaw());
            physics.value().set_character_velocity(player_body.value(), desired);
            const auto stepped = physics.value().update(kTick);
            if (!stepped) {
                HUE_LOG_ERROR("physics update failed");
                return 1;
            }

            // Physics -> ECS transforms (position + smoothed facing).
            hue::ecs::for_each(
                *body_pool.value(), *transform_pool.value(),
                [&](hue::ecs::Entity, BodyComponent& body, TransformComponent& transform) {
                    transform.position =
                        physics.value().character_position(body.character);
                    const hue::Vec3 velocity =
                        physics.value().character_velocity(body.character);
                    const hue::Vec3 planar{velocity.x, 0.0f, velocity.z};
                    if (length_squared(planar) > kIdleThreshold * kIdleThreshold) {
                        const float target_yaw = std::atan2(-planar.x, -planar.z);
                        const float delta = wrap_angle(target_yaw - transform.yaw);
                        transform.yaw += hue::damp(0.0f, delta, 14.0f, kTick);
                    }
                });

            if (animated_characters[0]) {
                const hue::Vec3 velocity =
                    physics.value().character_velocity(player_body.value());
                animated_characters[0]->set_locomotion_speed(
                    length(hue::Vec3{velocity.x, 0.0f, velocity.z}));
            }
            for (auto& character : animated_characters) {
                if (!character) continue;
                const auto animated =
                    character->update(kTick, animation_arena.value(), draws);
                if (!animated) {
                    HUE_LOG_ERROR("animation update failed");
                    return 1;
                }
            }
            if (const auto flushed = world.flush(); !flushed) {
                HUE_LOG_ERROR("ecs flush failed");
                return 1;
            }
        }

        if (renderer_active) {
            // ECS transforms -> draw list.
            hue::ecs::for_each(
                *draw_pool.value(), *transform_pool.value(),
                [&](hue::ecs::Entity, DrawComponent& draw, TransformComponent& transform) {
                    draws[draw.index].transform = hue::Mat4::trs(
                        transform.position,
                        hue::Quat::from_axis_angle({0.0f, 1.0f, 0.0f}, transform.yaw),
                        {1.0f, 1.0f, 1.0f});
                });

            if (use_fly_camera) {
                fly_camera.update(input, static_cast<float>(frame_seconds));
            } else {
                follow_camera.update(input, static_cast<float>(frame_seconds),
                                     physics.value().character_position(player_body.value()),
                                     physics.value());
            }
            const hue::RendererStatus& render_status = renderer.value().status();
            const float aspect =
                render_status.swapchain_height > 0
                    ? static_cast<float>(render_status.swapchain_width) /
                          static_cast<float>(render_status.swapchain_height)
                    : 16.0f / 9.0f;
            const hue::Camera camera = use_fly_camera ? fly_camera.camera(aspect)
                                                      : follow_camera.camera(aspect);
            const auto drawn = renderer.value().draw_frame(camera, draws, draw_count);
            if (!drawn) {
                HUE_LOG_ERROR("draw_frame failed; rendering disabled for this run");
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

    allocation_reporter.flush();
    HUE_LOG_INFO("shutdown after %lld frames, %lld sim steps", frames, timestep.total_steps());
    return 0;
}
