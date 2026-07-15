// game/src/main.cpp
//
// Week 1 loop: window + input + fixed timestep. Week 2 adds a fixed-storage
// allocation reporter until ImGui becomes the visual overlay in Week 13.
// --frames N exits after N frames (CI smoke test).

#include "hue/core/input.h"
#include "hue/core/log.h"
#include "hue/core/memory.h"
#include "hue/core/time.h"
#include "hue/core/trace.h"
#include "hue/core/version.h"
#include "hue/core/window.h"

#include <cstddef>
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

} // namespace

int main(int argc, char** argv) {
    hue::log::init(hue::log::Level::kDebug);
    hue::log::install_crash_handler();
    HUE_LOG_INFO("%s starting", hue::engine_version_string());

    long long max_frames = -1; // run until closed
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0) {
            max_frames = std::atoll(argv[i + 1]);
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
        // rendering with timestep.alpha() interpolation lands in Week 4

        ++frames;
        allocation_reporter.sample(frame_seconds);
        HUE_PROFILE_FRAME();
        if (max_frames >= 0 && frames >= max_frames)
            break;
    }

    allocation_reporter.flush();
    HUE_LOG_INFO("shutdown after %lld frames, %lld sim steps", frames, timestep.total_steps());
    return 0;
}
