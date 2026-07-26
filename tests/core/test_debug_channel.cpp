// tests/core/test_debug_channel.cpp
//
// The parser, response writer, and registry are pure (no sockets), so they
// get exhaustive unit tests here; the socket path is covered by the
// debug_channel_smoke CI test that drives a real game process over TCP.

#include <doctest/doctest.h>

#include "hue/core/debug_channel.h"

#include <cstring>
#include <string>

using hue::DebugCommandLine;
using hue::DebugCommandRegistry;
using hue::DebugResponse;

namespace {

// Registry is ~50 KB (fixed scratch buffer); keep it off the test stack.
DebugCommandRegistry g_registry;

std::string run_line(DebugCommandRegistry& registry, const char* text) {
    char line[hue::kDebugChannelMaxLine];
    std::strncpy(line, text, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    char out[hue::kDebugChannelResponseCapacity];
    DebugResponse response(out, sizeof(out));
    registry.execute_line(line, response);
    return std::string(response.c_str());
}

void echo_args(void* user_data, const DebugCommandLine& command, DebugResponse& response) {
    (void)user_data;
    response.append("{\"args\":[");
    for (std::uint32_t i = 0; i < command.arg_count; ++i) {
        if (i > 0) {
            response.append(",");
        }
        response.append_json_string(command.args[i]);
    }
    response.append("]}");
}

void always_fails(void* user_data, const DebugCommandLine& command, DebugResponse& response) {
    (void)user_data;
    (void)command;
    response.set_error("deliberate failure");
}

void no_payload(void* user_data, const DebugCommandLine& command, DebugResponse& response) {
    (void)user_data;
    (void)command;
    (void)response;
}

} // namespace

TEST_CASE("debug channel: command line parsing") {
    DebugCommandLine parsed;

    char simple[] = "status";
    CHECK(hue::parse_debug_command_line(simple, parsed));
    CHECK(std::strcmp(parsed.name, "status") == 0);
    CHECK(parsed.arg_count == 0);

    char with_args[] = "log.recent 25 verbose";
    CHECK(hue::parse_debug_command_line(with_args, parsed));
    CHECK(std::strcmp(parsed.name, "log.recent") == 0);
    REQUIRE(parsed.arg_count == 2);
    CHECK(std::strcmp(parsed.args[0], "25") == 0);
    CHECK(std::strcmp(parsed.args[1], "verbose") == 0);

    char padded[] = "  \t spawn   enemy  \t ";
    CHECK(hue::parse_debug_command_line(padded, parsed));
    CHECK(std::strcmp(parsed.name, "spawn") == 0);
    REQUIRE(parsed.arg_count == 1);
    CHECK(std::strcmp(parsed.args[0], "enemy") == 0);

    char empty[] = "";
    CHECK_FALSE(hue::parse_debug_command_line(empty, parsed));
    char blank[] = "   \t  ";
    CHECK_FALSE(hue::parse_debug_command_line(blank, parsed));

    char too_many[] = "cmd 1 2 3 4 5 6 7 8 9"; // 9 args > kDebugChannelMaxArgs (8)
    CHECK_FALSE(hue::parse_debug_command_line(too_many, parsed));
}

TEST_CASE("debug channel: u32 argument parsing") {
    std::uint32_t value = 0;
    CHECK(hue::parse_debug_u32("0", value));
    CHECK(value == 0);
    CHECK(hue::parse_debug_u32("46600", value));
    CHECK(value == 46600);
    CHECK(hue::parse_debug_u32("4294967295", value));
    CHECK(value == 4294967295u);

    CHECK_FALSE(hue::parse_debug_u32("4294967296", value)); // overflow
    CHECK_FALSE(hue::parse_debug_u32("-1", value));
    CHECK_FALSE(hue::parse_debug_u32("12x", value));
    CHECK_FALSE(hue::parse_debug_u32("", value));
    CHECK_FALSE(hue::parse_debug_u32(nullptr, value));
}

TEST_CASE("debug channel: response writer escapes JSON and bounds writes") {
    char buffer[64];
    DebugResponse response(buffer, sizeof(buffer));
    response.append_json_string("say \"hi\"\n\tc:\\path");
    CHECK(std::strcmp(response.c_str(), "\"say \\\"hi\\\"\\n\\tc:\\\\path\"") == 0);
    CHECK_FALSE(response.truncated());

    char tiny[8];
    DebugResponse small(tiny, sizeof(tiny));
    small.append("0123456789");
    CHECK(small.truncated());
    CHECK(small.length() == 7); // capacity - 1, always NUL-terminated
    CHECK(std::strcmp(small.c_str(), "0123456") == 0);
}

TEST_CASE("debug channel: registry dispatch and envelopes") {
    DebugCommandRegistry& registry = g_registry;
    REQUIRE(registry.register_command("echo", "echo arguments", &echo_args, nullptr).has_value());
    REQUIRE(registry.register_command("fail", "always fails", &always_fails, nullptr).has_value());
    REQUIRE(registry.register_command("noop", "no payload", &no_payload, nullptr).has_value());

    // Duplicate registration is rejected.
    CHECK_FALSE(registry.register_command("echo", "dup", &echo_args, nullptr).has_value());

    CHECK(run_line(registry, "echo one two") ==
          "{\"ok\":true,\"data\":{\"args\":[\"one\",\"two\"]}}\n");
    CHECK(run_line(registry, "noop") == "{\"ok\":true,\"data\":null}\n");
    CHECK(run_line(registry, "fail") == "{\"ok\":false,\"error\":\"deliberate failure\"}\n");
    CHECK(run_line(registry, "no.such.command") ==
          "{\"ok\":false,\"error\":\"unknown command (try: help)\"}\n");
    CHECK(run_line(registry, "") == "{\"ok\":false,\"error\":\"malformed command line\"}\n");

    // help is intrinsic and lists registered commands.
    const std::string help = run_line(registry, "help");
    CHECK(help.find("\"ok\":true") != std::string::npos);
    CHECK(help.find("\"name\":\"echo\"") != std::string::npos);
    CHECK(help.find("\"help\":\"echo arguments\"") != std::string::npos);
}
