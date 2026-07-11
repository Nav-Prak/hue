// engine/core/include/hue/core/log.h
//
// Leveled, ring-buffered logging (Week 1 spec). Messages land in a fixed
// in-memory ring (no heap) and echo to stderr at/above a console level.
// install_crash_handler() dumps the ring to hue-crash.log on fatal signals,
// so the last kRingCapacity messages survive a crash.

#pragma once

#include <cstddef>
#include <cstdint>

namespace hue::log {

enum class Level : std::uint8_t { kTrace = 0, kDebug, kInfo, kWarn, kError };

struct Entry {
    double time_ms; // since init()
    Level level;
    char message[240];
};

// Resets the ring and the clock. Call once at startup (tests call it per case).
void init(Level console_min = Level::kInfo);

void write(Level level, const char* fmt, ...);

// SIGSEGV / SIGABRT / SIGFPE / SIGILL: dump the ring, then re-raise.
void install_crash_handler();

// Writes the ring to a file, oldest entry first. Returns false if the file
// could not be opened.
bool dump(const char* path);

// Ring introspection (tests now, allocation/debug overlays later).
std::size_t ring_capacity();
std::size_t total_written();
// Copies up to max_entries of the most recent messages into out, oldest
// first. Returns the number copied.
std::size_t copy_recent(Entry* out, std::size_t max_entries);

const char* to_string(Level level);

} // namespace hue::log

#define HUE_LOG_TRACE(...) ::hue::log::write(::hue::log::Level::kTrace, __VA_ARGS__)
#define HUE_LOG_DEBUG(...) ::hue::log::write(::hue::log::Level::kDebug, __VA_ARGS__)
#define HUE_LOG_INFO(...) ::hue::log::write(::hue::log::Level::kInfo, __VA_ARGS__)
#define HUE_LOG_WARN(...) ::hue::log::write(::hue::log::Level::kWarn, __VA_ARGS__)
#define HUE_LOG_ERROR(...) ::hue::log::write(::hue::log::Level::kError, __VA_ARGS__)
