// tests/core/test_time.cpp

#include <doctest/doctest.h>

#include "hue/core/time.h"

TEST_CASE("fixed timestep: steady 60 fps yields one step per frame") {
    hue::FixedTimestep ts;
    int total = 0;
    for (int i = 0; i < 60; ++i) {
        total += ts.advance(1.0 / 60.0);
    }
    // floating point drift may shift a single step across the boundary
    CHECK(total >= 59);
    CHECK(total <= 61);
}

TEST_CASE("fixed timestep: 30 fps frames produce two steps each") {
    hue::FixedTimestep ts;
    CHECK(ts.advance(2.0 / 60.0) == 2);
    CHECK(ts.advance(2.0 / 60.0) == 2);
}

TEST_CASE("fixed timestep: fast frames accumulate before stepping") {
    hue::FixedTimestep ts;
    // 240 fps: one step every four frames
    int total = 0;
    for (int i = 0; i < 8; ++i) {
        total += ts.advance(1.0 / 240.0);
    }
    CHECK(total == 2);
}

// note: square brackets in doctest names break doctest_discover_tests
TEST_CASE("fixed timestep: alpha stays between 0 and 1") {
    hue::FixedTimestep ts;
    ts.advance(0.7 / 60.0);
    CHECK(ts.alpha() >= 0.0);
    CHECK(ts.alpha() < 1.0);
    CHECK(ts.alpha() == doctest::Approx(0.7));
}

TEST_CASE("fixed timestep: huge hitch is clamped, no spiral of death") {
    hue::FixedTimestep ts;
    const int steps = ts.advance(10.0); // debugger pause
    CHECK(steps <= (int)(hue::FixedTimestep::kMaxFrameSeconds * hue::FixedTimestep::kTickRate));
}

TEST_CASE("fixed timestep: negative frame time is ignored") {
    hue::FixedTimestep ts;
    CHECK(ts.advance(-1.0) == 0);
    CHECK(ts.alpha() == 0.0);
}
