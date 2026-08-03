// game/src/main.cpp
//
// Week 1 loop: window + input + fixed timestep. Week 2 adds a fixed-storage
// allocation reporter until ImGui becomes the visual overlay in Week 13.
// Week 3 spins up the job pool and runs a startup parallel-for benchmark.
// Week 4 brings up the Vulkan renderer (clear + triangle); machines with no
// Vulkan 1.3 device keep running windowed unless --require-renderer is set.
// --frames N exits after N frames (CI smoke test).

#include "hue/core/debug_channel.h"
#include "hue/core/input.h"
#include "hue/core/jobs.h"
#include "hue/core/log.h"
#include "hue/core/math.h"
#include "hue/core/memory.h"
#include "hue/core/time.h"
#include "hue/core/trace.h"
#include "hue/core/version.h"
#include "hue/core/window.h"
#include "hue/render/renderer.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

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
    response.append_format(",\"swapchain\":[%u,%u],\"frames_rendered\":%llu}",
                           status.swapchain_width, status.swapchain_height,
                           static_cast<unsigned long long>(status.frames_rendered));
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

    auto jobs = hue::JobSystem::create();
    if (!jobs) {
        HUE_LOG_ERROR("job system init failed");
        return 1;
    }
    HUE_LOG_INFO("job system: %u workers", jobs.value().worker_count());
    run_job_benchmark(jobs.value());

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

        const double frame_seconds = clock.tick();
        const int steps = timestep.advance(frame_seconds);
        for (int s = 0; s < steps; ++s) {
            log_input_edges(input); // stand-in for the sim tick
        }

        // Interpolation with timestep.alpha() starts mattering when the
        // camera moves (Week 5); the triangle only needs a present.
        if (renderer_active) {
            const auto drawn = renderer.value().draw_frame();
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
