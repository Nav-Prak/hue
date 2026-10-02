#pragma once
#include "encounter.h"
#include <cstddef>
#include <optional>

enum class FightSource { kInteractive, kAutomated, kInjected };
struct FightReport {
    EncounterState outcome = EncounterState::kReady;
    FightStats stats{};
    double wall_seconds = 0;
    std::uint32_t tuning_revision = 1;
    FightSource source = FightSource::kInteractive;
};

// Observe before AND after simulation. Ready resets collection; result frames
// emit only once. Time is a monotonic clock, never the clamped simulation delta.
class FightRecorder {
public:
    std::optional<FightReport> observe(const Encounter& encounter, double now,
                                       FightSource source) noexcept;
    std::optional<FightReport> observe(EncounterState state, const FightStats& stats,
                                       std::uint32_t revision, double now,
                                       FightSource source) noexcept;

private:
    bool m_running = false;
    double m_started = 0;
};

// Returns bytes written excluding NUL, or zero for invalid data/insufficient space.
std::size_t format_fight_report(char* output, std::size_t capacity,
                                const FightReport& report) noexcept;
// Explicit opt-in destination. Appends one JSON line; never creates directories.
bool append_fight_report(const char* path, const FightReport& report) noexcept;
