// tests/game/test_tuning.cpp
#include "tuning.h"
#include <doctest/doctest.h>
#include <limits>
constexpr char kTuning[] =
    R"({"version":1,"revision":1,"movement":[1.6,4,0.2],"dodge":[6,0.45],"vitals":[100,40,1,18],"input_buffer":0.35,"light_strike":[14,22,0.055,0.1],"heavy_strike":[28,45,0.09,0.18],"ai_ranges":[12,18,1.75],"ai_speeds":[1.6,3.2,1.5],"ai_times":[0.25,0.65,1.5],"ai_attacks":[0.65,1.8,2.4],"cancel_scale":[1,1],"lock_ranges":[15,20]})";
TEST_CASE("tuning: bounded schema and relational validation") {
    auto parsed = parse_tuning(kTuning, sizeof(kTuning) - 1);
    REQUIRE(parsed);
    CHECK(parsed.value().run_speed == 4.0f);
    for (std::size_t i = 0; i < sizeof(kTuning) - 1; ++i)
        CHECK_FALSE(parse_tuning(kTuning, i));
    GameTuning t;
    CHECK(valid_tuning(t));
    t.run_speed = t.walk_speed;
    CHECK_FALSE(valid_tuning(t));
    t = {};
    t.ai.attack_range = 30;
    CHECK_FALSE(valid_tuning(t));
    t = {};
    t.light.hitstop_seconds = -1;
    CHECK_FALSE(valid_tuning(t));
    t = {};
    t.heavy.health = std::numeric_limits<float>::infinity();
    CHECK_FALSE(valid_tuning(t));
    t = {};
    t.revision = 0;
    CHECK_FALSE(valid_tuning(t));
    t = {};
    t.lock_release = 1;
    CHECK_FALSE(valid_tuning(t));
}
