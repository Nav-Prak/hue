#include "fight_report.h"
#include <cstring>
#include <doctest/doctest.h>
#include <limits>

TEST_CASE("fight report: monotonic time excludes ready and emits each result once") {
    FightRecorder recorder;
    FightStats stats;
    stats.ticks = 120;
    stats.hitstop_ticks = 12;
    stats.hits[0] = 3;
    stats.staggers[0] = 1;
    const auto source = FightSource::kInteractive;
    CHECK_FALSE(recorder.observe(EncounterState::kReady, stats, 2, 1, source));
    CHECK_FALSE(recorder.observe(EncounterState::kFighting, stats, 2, 100, source));
    CHECK_FALSE(recorder.observe(EncounterState::kFighting, stats, 2, 101, source));
    const auto report = recorder.observe(EncounterState::kWon, stats, 2, 102.5, source);
    REQUIRE(report);
    CHECK(report->wall_seconds == 2.5);
    CHECK(report->tuning_revision == 2);
    CHECK_FALSE(recorder.observe(EncounterState::kWon, stats, 2, 400, source));
    char output[1024];
    REQUIRE(format_fight_report(output, sizeof(output), *report));
    CHECK(std::strstr(output, "\"simulation_seconds\":2.000000"));
    CHECK(std::strstr(output, "\"active_seconds\":1.800000"));
    CHECK(std::strstr(output, "\"source\":\"interactive\""));
    CHECK_FALSE(recorder.observe(EncounterState::kReady, stats, 2, 500, source));
    CHECK_FALSE(recorder.observe(EncounterState::kFighting, stats, 3, 501, source));
    CHECK(recorder.observe(EncounterState::kLost, stats, 3, 502, FightSource::kInjected));
}
TEST_CASE("fight report: abandoned fights and invalid or oversized data are rejected") {
    FightRecorder recorder;
    FightStats stats;
    CHECK_FALSE(
        recorder.observe(EncounterState::kFighting, stats, 1, 1, FightSource::kInteractive));
    CHECK_FALSE(recorder.observe(EncounterState::kReady, stats, 1, 2, FightSource::kInteractive));
    CHECK_FALSE(recorder.observe(EncounterState::kWon, stats, 1, 3, FightSource::kInteractive));
    FightReport report{EncounterState::kLost, {}, 1, 1, FightSource::kInjected};
    report.stats.ticks = 60;
    char output[1024];
    CHECK(format_fight_report(output, sizeof(output), report));
    CHECK_FALSE(format_fight_report(output, 8, report));
    CHECK(output[0] == '\0');
    report.wall_seconds = std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(format_fight_report(output, sizeof(output), report));
    report.wall_seconds = 1;
    report.stats.hitstop_ticks = 61;
    CHECK_FALSE(format_fight_report(output, sizeof(output), report));
    CHECK_FALSE(append_fight_report(nullptr, report));
}
