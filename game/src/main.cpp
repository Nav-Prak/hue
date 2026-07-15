// game/src/main.cpp
//
// Week 1 loop: window + input + fixed timestep. The sim step just logs
// input edges for now (DoD: gamepad input logged); rendering arrives with
// Vulkan in Week 4. --frames N exits after N frames (CI smoke test).

#include "hue/core/input.h"
#include "hue/core/log.h"
#include "hue/core/time.h"
#include "hue/core/trace.h"
#include "hue/core/version.h"
#include "hue/core/window.h"

#include <cstdlib>
#include <cstring>

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

    if (input.key_pressed(key::kSpace)) HUE_LOG_INFO("space pressed");
    if (input.mouse_pressed(mouse::kLeft)) {
        HUE_LOG_INFO("mouse left at (%.0f, %.0f)", input.mouse_x(), input.mouse_y());
    }
}

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
    bool gamepad_was_connected = false;
    long long frames = 0;

    while (!window.value().should_close()) {
        HUE_PROFILE_ZONE("game::frame");
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
        if (input.key_pressed(hue::key::kEscape)) break;

        const int steps = timestep.advance(clock.tick());
        for (int s = 0; s < steps; ++s) {
            log_input_edges(input); // stand-in for the sim tick
        }
        // rendering with timestep.alpha() interpolation lands in Week 4

        ++frames;
        HUE_PROFILE_FRAME();
        if (max_frames >= 0 && frames >= max_frames) break;
    }

    HUE_LOG_INFO("shutdown after %lld frames, %lld sim steps", frames, timestep.total_steps());
    return 0;
}
