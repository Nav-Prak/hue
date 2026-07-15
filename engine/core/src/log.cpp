// engine/core/src/log.cpp

#include "hue/core/log.h"

#include "hue/core/trace.h"

#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace hue::log {

namespace {

constexpr std::size_t kRingCapacity = 1024;

struct State {
    Entry ring[kRingCapacity];
    std::size_t total = 0; // monotonic; ring slot = total % capacity
    Level console_min = Level::kInfo;
    std::chrono::steady_clock::time_point start;
    std::mutex mutex;
};

State& state() {
    static State s;
    return s;
}

void crash_signal_handler(int sig) {
    // Signal-safety is deliberately traded for post-mortem value: the process
    // is dying anyway, and the ring dump is the whole point of the handler.
    dump("hue-crash.log");
    std::fprintf(stderr, "hue: fatal signal %d, log ring dumped to hue-crash.log\n", sig);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

} // namespace

const char* to_string(Level level) {
    switch (level) {
    case Level::kTrace: return "TRACE";
    case Level::kDebug: return "DEBUG";
    case Level::kInfo: return "INFO";
    case Level::kWarn: return "WARN";
    case Level::kError: return "ERROR";
    }
    return "?";
}

void init(Level console_min) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.total = 0;
    s.console_min = console_min;
    s.start = std::chrono::steady_clock::now();
}

void write(Level level, const char* fmt, ...) {
    HUE_PROFILE_ZONE("log::write");
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);

    Entry& e = s.ring[s.total % kRingCapacity];
    e.level = level;
    e.time_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s.start)
            .count();

    va_list args;
    va_start(args, fmt);
    std::vsnprintf(e.message, sizeof(e.message), fmt, args);
    va_end(args);

    ++s.total;

    if (level >= s.console_min) {
        std::fprintf(stderr, "[%10.3f] %-5s %s\n", e.time_ms, to_string(level), e.message);
    }
}

void install_crash_handler() {
    std::signal(SIGSEGV, crash_signal_handler);
    std::signal(SIGABRT, crash_signal_handler);
    std::signal(SIGFPE, crash_signal_handler);
    std::signal(SIGILL, crash_signal_handler);
}

bool dump(const char* path) {
    // No lock: called from the crash handler, where the mutex may already be
    // held by the crashing thread. Racing writers can garble one entry at
    // worst; a deadlocked handler loses everything.
    State& s = state();
    std::FILE* f = std::fopen(path, "w");
    if (!f) return false;

    const std::size_t count = (s.total < kRingCapacity) ? s.total : kRingCapacity;
    const std::size_t first = s.total - count;
    for (std::size_t i = 0; i < count; ++i) {
        const Entry& e = s.ring[(first + i) % kRingCapacity];
        std::fprintf(f, "[%10.3f] %-5s %s\n", e.time_ms, to_string(e.level), e.message);
    }
    std::fclose(f);
    return true;
}

std::size_t ring_capacity() {
    return kRingCapacity;
}

std::size_t total_written() {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.total;
}

std::size_t copy_recent(Entry* out, std::size_t max_entries) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    std::size_t count = (s.total < kRingCapacity) ? s.total : kRingCapacity;
    if (count > max_entries) count = max_entries;
    const std::size_t first = s.total - count;
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = s.ring[(first + i) % kRingCapacity];
    }
    return count;
}

} // namespace hue::log
