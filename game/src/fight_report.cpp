#include "fight_report.h"
#include <cmath>
#include <cstdio>

std::optional<FightReport> FightRecorder::observe(const Encounter& encounter, double now,
                                                  FightSource source) noexcept {
    return observe(encounter.state(), encounter.stats(), encounter.tuning().revision, now, source);
}
std::optional<FightReport> FightRecorder::observe(EncounterState state, const FightStats& stats,
                                                  std::uint32_t revision, double now,
                                                  FightSource source) noexcept {
    if (!std::isfinite(now) || now < 0)
        return {};
    if (state == EncounterState::kReady)
        m_running = false;
    else if (state == EncounterState::kFighting && !m_running) {
        m_started = now;
        m_running = true;
    } else if ((state == EncounterState::kWon || state == EncounterState::kLost) && m_running) {
        m_running = false;
        if (now >= m_started)
            return FightReport{state, stats, now - m_started, revision, source};
    }
    return {};
}

std::size_t format_fight_report(char* output, std::size_t capacity,
                                const FightReport& report) noexcept {
    if (!output || capacity == 0)
        return 0;
    output[0] = '\0';
    const auto& s = report.stats;
    const char* source = nullptr;
    switch (report.source) {
    case FightSource::kInteractive:
        source = "interactive";
        break;
    case FightSource::kAutomated:
        source = "automated";
        break;
    case FightSource::kInjected:
        source = "injected";
        break;
    }
    if (!source ||
        (report.outcome != EncounterState::kWon && report.outcome != EncounterState::kLost) ||
        !std::isfinite(report.wall_seconds) || report.wall_seconds < 0 ||
        report.wall_seconds > 1e9 || report.tuning_revision == 0 || s.ticks == 0 ||
        s.ticks > 9007199254740991ull || s.hitstop_ticks > s.ticks || s.hits[0] > s.ticks ||
        s.hits[1] > s.ticks || s.dodges > s.ticks || s.staggers[0] > s.hits[0] ||
        s.staggers[1] > s.hits[1])
        return 0;
    const int written = std::snprintf(
        output, capacity,
        "{\"schema\":1,\"source\":\"%s\",\"outcome\":\"%s\",\"seed\":2026,"
        "\"tuning_revision\":%u,\"ticks\":%llu,\"hitstop_ticks\":%llu,"
        "\"simulation_seconds\":%.6f,\"active_seconds\":%.6f,\"wall_seconds\":%.6f,"
        "\"hits_landed\":%u,\"hits_taken\":%u,\"staggers_inflicted\":%u,"
        "\"staggers_received\":%u,\"dodges\":%u}\n",
        source, report.outcome == EncounterState::kWon ? "victory" : "defeat",
        report.tuning_revision, static_cast<unsigned long long>(s.ticks),
        static_cast<unsigned long long>(s.hitstop_ticks), static_cast<double>(s.ticks) / 60.0,
        static_cast<double>(s.ticks - s.hitstop_ticks) / 60.0, report.wall_seconds, s.hits[0],
        s.hits[1], s.staggers[0], s.staggers[1], s.dodges);
    if (written < 0 || static_cast<std::size_t>(written) >= capacity) {
        output[0] = '\0';
        return 0;
    }
    return static_cast<std::size_t>(written);
}
bool append_fight_report(const char* path, const FightReport& report) noexcept {
    char line[1024];
    const auto size = format_fight_report(line, sizeof(line), report);
    if (!path || !*path || !size)
        return false;
    auto* file = std::fopen(path, "ab");
    if (!file)
        return false;
    const bool written = std::fwrite(line, 1, size, file) == size;
    const bool closed = std::fclose(file) == 0;
    return written && closed;
}
