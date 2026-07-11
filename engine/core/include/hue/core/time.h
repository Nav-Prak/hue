// engine/core/include/hue/core/time.h
//
// Fixed-timestep accumulator with render interpolation (Week 1 spec).
// Simulation advances in exact kTickRate steps regardless of frame rate;
// alpha() says how far between the last two sim states the renderer should
// interpolate. Header-only and clock-free so tests can drive it with
// synthetic frame times.

#pragma once

#include <chrono>

namespace hue {

class FixedTimestep {
public:
    static constexpr double kTickRate = 60.0;
    static constexpr double kTickSeconds = 1.0 / kTickRate;
    // A debugger pause or hitch shouldn't spiral: clamp one frame's worth of
    // catch-up, accepting slow-motion beyond it.
    static constexpr double kMaxFrameSeconds = 0.25;

    // Feed one frame's wall-clock duration; returns how many fixed sim steps
    // to run this frame.
    int advance(double frame_seconds) {
        if (frame_seconds < 0.0) frame_seconds = 0.0;
        if (frame_seconds > kMaxFrameSeconds) frame_seconds = kMaxFrameSeconds;
        m_accumulator += frame_seconds;
        int steps = 0;
        while (m_accumulator >= kTickSeconds) {
            m_accumulator -= kTickSeconds;
            ++steps;
        }
        m_total_steps += steps;
        return steps;
    }

    // 0..1: fraction of a tick left in the accumulator, for render interpolation.
    double alpha() const { return m_accumulator / kTickSeconds; }

    long long total_steps() const { return m_total_steps; }

private:
    double m_accumulator = 0.0;
    long long m_total_steps = 0;
};

// Wall-clock frame timer feeding FixedTimestep in the real loop.
class FrameClock {
public:
    FrameClock() : m_last(std::chrono::steady_clock::now()) {}

    // Seconds since the previous tick() (or construction).
    double tick() {
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - m_last).count();
        m_last = now;
        return dt;
    }

private:
    std::chrono::steady_clock::time_point m_last;
};

} // namespace hue
