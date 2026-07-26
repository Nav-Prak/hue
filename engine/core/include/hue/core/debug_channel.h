// engine/core/include/hue/core/debug_channel.h
//
// Localhost debug command channel: quake-console-style text commands in,
// one JSON object per line out. This is the engine's agent-facing surface —
// the MCP server in tools/mcp forwards AI agent requests to it.
//
// Security posture (this is an untrusted input boundary, same rules as
// asset parsing): binds 127.0.0.1 only, one client at a time, hard caps on
// line length / argument count / commands per frame, malformed input gets a
// JSON error and is never interpreted further. The line parser is a pure
// function so it can join the fuzz harnesses in Week 14. Compiled out
// entirely unless HUE_DEBUG_CHANNEL is defined, and the game only opens the
// socket behind an explicit --debug-channel flag.

#pragma once

#include <cstddef>
#include <cstdint>

#include "hue/core/result.h"

namespace hue {

inline constexpr std::uint16_t kDebugChannelDefaultPort = 46600;
inline constexpr std::size_t kDebugChannelMaxLine = 4096;
inline constexpr std::size_t kDebugChannelMaxArgs = 8;
inline constexpr std::size_t kDebugChannelMaxCommands = 64;
inline constexpr std::size_t kDebugChannelMaxCommandsPerFrame = 16;
inline constexpr std::size_t kDebugChannelPayloadCapacity = 48 * 1024;
inline constexpr std::size_t kDebugChannelResponseCapacity = 64 * 1024;

// ------------------------------------------------------------- parsing

// A tokenized command line: name plus up to kDebugChannelMaxArgs arguments.
// Pointers refer into the caller's (mutated) line buffer.
struct DebugCommandLine {
    const char* name = nullptr;
    const char* args[kDebugChannelMaxArgs] = {};
    std::uint32_t arg_count = 0;
};

// Tokenizes `line` in place (splits on spaces/tabs, writes NULs). Returns
// false for an empty line or one with more than kDebugChannelMaxArgs
// arguments. Pure function: no allocation, no globals — fuzzable.
[[nodiscard]] bool parse_debug_command_line(char* line, DebugCommandLine& out) noexcept;

// Strict decimal u32 parse for command arguments. Returns false on empty,
// non-digit characters, or overflow.
[[nodiscard]] bool parse_debug_u32(const char* text, std::uint32_t& out) noexcept;

// ------------------------------------------------------------- response

// Bounded writer command handlers use to emit their JSON payload. Overflow
// sets the truncated flag instead of writing out of bounds; the registry
// turns a truncated payload into an error response.
class DebugResponse {
public:
    DebugResponse(char* buffer, std::size_t capacity) noexcept
        : m_buffer(buffer), m_capacity(capacity) {
        if (m_capacity > 0) {
            m_buffer[0] = '\0';
        }
    }

    // Appends raw text; the caller guarantees it is a valid JSON fragment.
    void append(const char* text) noexcept;
    // printf-style append.
    void append_format(const char* fmt, ...) noexcept;
    // Appends `text` as a quoted JSON string, escaping quotes, backslashes,
    // and control characters.
    void append_json_string(const char* text) noexcept;

    // Marks this command as failed; the registry emits an error envelope
    // with this message instead of the payload. Message must be static or
    // outlive the handler call.
    void set_error(const char* message) noexcept { m_error = message; }

    [[nodiscard]] const char* c_str() const noexcept { return m_buffer; }
    [[nodiscard]] std::size_t length() const noexcept { return m_length; }
    [[nodiscard]] bool truncated() const noexcept { return m_truncated; }
    [[nodiscard]] const char* error() const noexcept { return m_error; }

private:
    char* m_buffer = nullptr;
    std::size_t m_capacity = 0;
    std::size_t m_length = 0;
    bool m_truncated = false;
    const char* m_error = nullptr;
};

// ------------------------------------------------------------- registry

// Handlers write their JSON payload into `response`; args excludes the
// command name itself.
using DebugCommandFn = void (*)(void* user_data, const DebugCommandLine& command,
                                DebugResponse& response);

// Fixed-capacity name -> handler table plus the response envelope logic.
// Separate from the socket so it is unit-testable without networking.
class DebugCommandRegistry {
public:
    DebugCommandRegistry();

    // `name` and `help` must be string literals (or otherwise outlive the
    // registry). Fails with kInvalidArgument on duplicates or a full table.
    Result<void> register_command(const char* name, const char* help, DebugCommandFn fn,
                                  void* user_data) noexcept;

    // Parses and dispatches one command line (mutated in place), writing a
    // complete envelope into `out`:
    //   {"ok":true,"data":<payload>}   or   {"ok":false,"error":"..."}
    // A trailing newline is appended. Never fails: bad input produces an
    // error envelope.
    void execute_line(char* line, DebugResponse& out) noexcept;

    [[nodiscard]] std::size_t command_count() const noexcept { return m_count; }

private:
    static void help_command(void* user_data, const DebugCommandLine& command,
                             DebugResponse& response);

    struct Command {
        const char* name = nullptr;
        const char* help = nullptr;
        DebugCommandFn fn = nullptr;
        void* user_data = nullptr;
    };

    Command m_commands[kDebugChannelMaxCommands];
    std::size_t m_count = 0;
    char m_payload[kDebugChannelPayloadCapacity];
};

// ------------------------------------------------------------- channel

#if defined(HUE_DEBUG_CHANNEL)

// Non-blocking localhost TCP server pumped from the game loop. Port 0 asks
// the OS for an ephemeral port (tests); read the real one back with port().
class DebugChannel {
public:
    [[nodiscard]] static Result<DebugChannel> create(std::uint16_t port = kDebugChannelDefaultPort);

    DebugChannel(DebugChannel&& other) noexcept;
    DebugChannel(const DebugChannel&) = delete;
    DebugChannel& operator=(const DebugChannel&) = delete;
    DebugChannel& operator=(DebugChannel&&) = delete;
    ~DebugChannel();

    // Accepts a pending client, reads available bytes, and dispatches at
    // most kDebugChannelMaxCommandsPerFrame complete lines. Call once per
    // frame; costs two non-blocking syscalls when idle.
    void update() noexcept;

    [[nodiscard]] Result<void> register_command(const char* name, const char* help,
                                                DebugCommandFn fn, void* user_data) noexcept;

    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] bool client_connected() const noexcept;

private:
    DebugChannel() = default;

    struct State; // sockets + buffers, defined in debug_channel.cpp

    State* m_state = nullptr;
};

#endif // HUE_DEBUG_CHANNEL

} // namespace hue
