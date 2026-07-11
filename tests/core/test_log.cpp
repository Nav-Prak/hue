// tests/core/test_log.cpp

#include <doctest/doctest.h>

#include "hue/core/log.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
// quiet console: only errors escape to stderr during tests
void reset_log() {
    hue::log::init(hue::log::Level::kError);
}
} // namespace

TEST_CASE("log: messages are formatted and counted") {
    reset_log();
    HUE_LOG_INFO("hello %s %d", "world", 42);
    CHECK(hue::log::total_written() == 1);

    hue::log::Entry e;
    REQUIRE(hue::log::copy_recent(&e, 1) == 1);
    CHECK(std::strcmp(e.message, "hello world 42") == 0);
    CHECK(e.level == hue::log::Level::kInfo);
}

TEST_CASE("log: ring keeps only the newest capacity entries") {
    reset_log();
    const std::size_t cap = hue::log::ring_capacity();
    for (std::size_t i = 0; i < cap + 10; ++i) {
        HUE_LOG_DEBUG("msg %zu", i);
    }
    CHECK(hue::log::total_written() == cap + 10);

    std::vector<hue::log::Entry> entries(cap);
    const std::size_t got = hue::log::copy_recent(entries.data(), cap);
    REQUIRE(got == cap);
    // oldest surviving entry is #10, newest is #(cap+9)
    CHECK(std::strcmp(entries.front().message, "msg 10") == 0);
    char last[64];
    std::snprintf(last, sizeof(last), "msg %zu", cap + 9);
    CHECK(std::strcmp(entries.back().message, last) == 0);
}

TEST_CASE("log: oversized message is truncated, not overflowed") {
    reset_log();
    const std::string big(1000, 'x');
    HUE_LOG_INFO("%s", big.c_str());

    hue::log::Entry e;
    REQUIRE(hue::log::copy_recent(&e, 1) == 1);
    CHECK(std::strlen(e.message) < sizeof(e.message));
}

TEST_CASE("log: dump writes the ring to disk oldest-first") {
    reset_log();
    HUE_LOG_INFO("first");
    HUE_LOG_WARN("second");
    const char* path = "test-log-dump.tmp";
    REQUIRE(hue::log::dump(path));

    std::FILE* f = std::fopen(path, "rb");
    REQUIRE(f != nullptr);
    char buf[512] = {};
    const std::size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    std::remove(path);

    const std::string text(buf, n);
    const auto p1 = text.find("first");
    const auto p2 = text.find("second");
    REQUIRE(p1 != std::string::npos);
    REQUIRE(p2 != std::string::npos);
    CHECK(p1 < p2);
    CHECK(text.find("WARN") != std::string::npos);
}
