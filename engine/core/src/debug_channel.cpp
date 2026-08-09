// engine/core/src/debug_channel.cpp

#include "hue/core/debug_channel.h"

#include "hue/core/log.h"
#include "hue/core/memory.h"
#include "hue/core/version.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>

#if defined(HUE_DEBUG_CHANNEL)
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#endif // HUE_DEBUG_CHANNEL

namespace hue {

// ------------------------------------------------------------- parsing

bool parse_debug_command_line(char* line, DebugCommandLine& out) noexcept {
    out = DebugCommandLine{};
    if (line == nullptr) {
        return false;
    }

    char* cursor = line;
    for (;;) {
        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        char* token = cursor;
        while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
            ++cursor;
        }
        if (*cursor != '\0') {
            *cursor = '\0';
            ++cursor;
        }
        if (out.name == nullptr) {
            out.name = token;
        } else {
            if (out.arg_count >= kDebugChannelMaxArgs) {
                return false;
            }
            out.args[out.arg_count] = token;
            ++out.arg_count;
        }
    }
    return out.name != nullptr;
}

bool parse_debug_u32(const char* text, std::uint32_t& out) noexcept {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    std::uint64_t value = 0;
    for (const char* c = text; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9') {
            return false;
        }
        value = value * 10u + static_cast<std::uint64_t>(*c - '0');
        if (value > 0xFFFFFFFFull) {
            return false;
        }
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

// ------------------------------------------------------------- response

void DebugResponse::append(const char* text) noexcept {
    if (text == nullptr || m_capacity == 0) {
        return;
    }
    const std::size_t text_length = std::strlen(text);
    const std::size_t remaining = m_capacity - 1 - m_length;
    const std::size_t copied = text_length <= remaining ? text_length : remaining;
    std::memcpy(m_buffer + m_length, text, copied);
    m_length += copied;
    m_buffer[m_length] = '\0';
    if (copied < text_length) {
        m_truncated = true;
    }
}

void DebugResponse::append_format(const char* fmt, ...) noexcept {
    if (m_capacity == 0) {
        return;
    }
    const std::size_t remaining = m_capacity - m_length;
    va_list args;
    va_start(args, fmt);
    const int written = std::vsnprintf(m_buffer + m_length, remaining, fmt, args);
    va_end(args);
    if (written < 0) {
        m_truncated = true;
        return;
    }
    if (static_cast<std::size_t>(written) >= remaining) {
        m_length = m_capacity - 1;
        m_truncated = true;
    } else {
        m_length += static_cast<std::size_t>(written);
    }
}

void DebugResponse::append_json_string(const char* text) noexcept {
    append("\"");
    if (text != nullptr) {
        char escape_buffer[8];
        for (const char* c = text; *c != '\0'; ++c) {
            const unsigned char ch = static_cast<unsigned char>(*c);
            switch (ch) {
            case '"':
                append("\\\"");
                break;
            case '\\':
                append("\\\\");
                break;
            case '\n':
                append("\\n");
                break;
            case '\r':
                append("\\r");
                break;
            case '\t':
                append("\\t");
                break;
            default:
                if (ch < 0x20) {
                    std::snprintf(escape_buffer, sizeof(escape_buffer), "\\u%04x",
                                  static_cast<unsigned>(ch));
                    append(escape_buffer);
                } else {
                    const char single[2] = {*c, '\0'};
                    append(single);
                }
                break;
            }
        }
    }
    append("\"");
}

// ------------------------------------------------------------- registry

namespace {

void write_error_envelope(DebugResponse& out, const char* message) noexcept {
    out.append("{\"ok\":false,\"error\":");
    out.append_json_string(message);
    out.append("}\n");
}

} // namespace

DebugCommandRegistry::DebugCommandRegistry() {
    // help is intrinsic: it needs the table itself.
    const auto registered =
        register_command("help", "list registered commands", &help_command, this);
    (void)registered;
}

Result<void> DebugCommandRegistry::register_command(const char* name, const char* help,
                                                    DebugCommandFn fn, void* user_data) noexcept {
    if (name == nullptr || fn == nullptr || m_count >= kDebugChannelMaxCommands) {
        return ErrorCode::kInvalidArgument;
    }
    for (std::size_t i = 0; i < m_count; ++i) {
        if (std::strcmp(m_commands[i].name, name) == 0) {
            return ErrorCode::kInvalidArgument;
        }
    }
    m_commands[m_count] = Command{name, help != nullptr ? help : "", fn, user_data};
    ++m_count;
    return {};
}

void DebugCommandRegistry::execute_line(char* line, DebugResponse& out) noexcept {
    DebugCommandLine command;
    if (!parse_debug_command_line(line, command)) {
        write_error_envelope(out, "malformed command line");
        return;
    }

    const Command* found = nullptr;
    for (std::size_t i = 0; i < m_count; ++i) {
        if (std::strcmp(m_commands[i].name, command.name) == 0) {
            found = &m_commands[i];
            break;
        }
    }
    if (found == nullptr) {
        write_error_envelope(out, "unknown command (try: help)");
        return;
    }

    DebugResponse payload(m_payload, kDebugChannelPayloadCapacity);
    found->fn(found->user_data, command, payload);

    if (payload.error() != nullptr) {
        write_error_envelope(out, payload.error());
        return;
    }
    if (payload.truncated()) {
        write_error_envelope(out, "response payload truncated");
        return;
    }
    out.append("{\"ok\":true,\"data\":");
    out.append(payload.length() > 0 ? payload.c_str() : "null");
    out.append("}\n");
}

void DebugCommandRegistry::help_command(void* user_data, const DebugCommandLine& command,
                                        DebugResponse& response) {
    (void)command;
    const auto* registry = static_cast<const DebugCommandRegistry*>(user_data);
    response.append("{\"commands\":[");
    for (std::size_t i = 0; i < registry->m_count; ++i) {
        if (i > 0) {
            response.append(",");
        }
        response.append("{\"name\":");
        response.append_json_string(registry->m_commands[i].name);
        response.append(",\"help\":");
        response.append_json_string(registry->m_commands[i].help);
        response.append("}");
    }
    response.append("]}");
}

// ------------------------------------------------------------- channel

#if defined(HUE_DEBUG_CHANNEL)

namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

void close_socket(SocketHandle socket_handle) noexcept {
#if defined(_WIN32)
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
}

[[nodiscard]] bool last_error_would_block() noexcept {
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

[[nodiscard]] bool set_non_blocking(SocketHandle socket_handle) noexcept {
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(socket_handle, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket_handle, F_GETFL, 0);
    return flags >= 0 && fcntl(socket_handle, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void set_no_delay(SocketHandle socket_handle) noexcept {
    int enable = 1;
    setsockopt(socket_handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enable),
               sizeof(enable));
}

// Blocking-ish send with a bounded wait: responses are small and the peer
// is localhost, so the socket buffer virtually never fills. If it does, a
// few short select() waits; still stuck -> report failure (caller drops the
// client rather than stalling the frame loop indefinitely).
[[nodiscard]] bool send_all(SocketHandle socket_handle, const char* data,
                            std::size_t length) noexcept {
    std::size_t sent_total = 0;
    int wait_budget = 5;
    while (sent_total < length) {
        const auto sent = send(socket_handle, data + sent_total,
#if defined(_WIN32)
                               static_cast<int>(length - sent_total),
#else
                               length - sent_total,
#endif
                               0);
        if (sent > 0) {
            sent_total += static_cast<std::size_t>(sent);
            continue;
        }
        if (!last_error_would_block() || wait_budget == 0) {
            return false;
        }
        --wait_budget;
        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(socket_handle, &write_set);
        timeval timeout{};
        timeout.tv_usec = 50 * 1000; // 50 ms
        select(static_cast<int>(socket_handle) + 1, nullptr, &write_set, nullptr, &timeout);
    }
    return true;
}

// ------------------------------------------ core built-in commands

void version_command(void* user_data, const DebugCommandLine& command, DebugResponse& response) {
    (void)user_data;
    (void)command;
    response.append("{\"version\":");
    response.append_json_string(engine_version_string());
    response.append("}");
}

void log_recent_command(void* user_data, const DebugCommandLine& command,
                        DebugResponse& response) {
    (void)user_data;
    constexpr std::uint32_t kMaxEntries = 128;
    std::uint32_t requested = 50;
    if (command.arg_count >= 1 && !parse_debug_u32(command.args[0], requested)) {
        response.set_error("log.recent expects a numeric count");
        return;
    }
    if (requested > kMaxEntries) {
        requested = kMaxEntries;
    }

    // Single-threaded frame loop; static avoids 32 KB on the stack.
    static log::Entry entries[kMaxEntries];
    const std::size_t copied = log::copy_recent(entries, requested);

    response.append("{\"entries\":[");
    for (std::size_t i = 0; i < copied; ++i) {
        if (i > 0) {
            response.append(",");
        }
        response.append_format("{\"time_ms\":%.3f,\"level\":", entries[i].time_ms);
        response.append_json_string(log::to_string(entries[i].level));
        response.append(",\"message\":");
        response.append_json_string(entries[i].message);
        response.append("}");
    }
    response.append("]}");
}

void mem_snapshot_command(void* user_data, const DebugCommandLine& command,
                          DebugResponse& response) {
    (void)user_data;
    (void)command;
    const AllocationSnapshot snapshot = allocation_snapshot();
    response.append("{\"tags\":[");
    for (std::size_t i = 0; i < kMemoryTagCount; ++i) {
        const AllocationStats& stats = snapshot.tags[i];
        if (i > 0) {
            response.append(",");
        }
        response.append("{\"tag\":");
        response.append_json_string(memory_tag_name(static_cast<MemoryTag>(i)));
        response.append_format(",\"current_bytes\":%zu,\"peak_bytes\":%zu,"
                               "\"live_allocations\":%zu,\"total_allocated_bytes\":%zu}",
                               stats.current_bytes, stats.peak_bytes, stats.live_allocations,
                               stats.total_allocated_bytes);
    }
    response.append("]}");
}

} // namespace

struct DebugChannel::State {
    SocketHandle listen_socket = kInvalidSocket;
    SocketHandle client_socket = kInvalidSocket;
    std::uint16_t port = 0;
    bool discarding_long_line = false;
    std::size_t line_length = 0;
    char line_buffer[kDebugChannelMaxLine];
    char response_buffer[kDebugChannelResponseCapacity];
    DebugCommandRegistry registry;
#if defined(_WIN32)
    bool winsock_initialized = false;
#endif
};

Result<DebugChannel> DebugChannel::create(std::uint16_t port) {
    auto allocation = heap_allocate(sizeof(State), alignof(State), MemoryTag::kCore);
    if (!allocation) {
        return allocation.error();
    }
    State* state = ::new (allocation.value()) State();

    const auto fail = [state](const char* reason) -> Result<DebugChannel> {
        HUE_LOG_ERROR("debug channel: %s", reason);
        if (state->listen_socket != kInvalidSocket) {
            close_socket(state->listen_socket);
        }
#if defined(_WIN32)
        if (state->winsock_initialized) {
            WSACleanup();
        }
#endif
        state->~State();
        const auto freed = heap_free(state);
        (void)freed;
        return ErrorCode::kUnknown;
    };

#if defined(_WIN32)
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return fail("WSAStartup failed");
    }
    state->winsock_initialized = true;
#endif

    state->listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (state->listen_socket == kInvalidSocket) {
        return fail("socket() failed");
    }

    // SO_REUSEADDR lets a quick CI re-run reuse a port still in TIME_WAIT.
    // On Windows this is also required for some rapid restart scenarios.
    {
        const int reuse = 1;
        setsockopt(state->listen_socket, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // never a public interface
    address.sin_port = htons(port);
    if (bind(state->listen_socket, reinterpret_cast<const sockaddr*>(&address),
             sizeof(address)) != 0) {
        return fail("bind to 127.0.0.1 failed (port in use?)");
    }
    if (listen(state->listen_socket, 1) != 0) {
        return fail("listen() failed");
    }

    sockaddr_in bound{};
#if defined(_WIN32)
    int bound_length = sizeof(bound);
#else
    socklen_t bound_length = sizeof(bound);
#endif
    if (getsockname(state->listen_socket, reinterpret_cast<sockaddr*>(&bound), &bound_length) !=
        0) {
        return fail("getsockname() failed");
    }
    state->port = ntohs(bound.sin_port);

    if (!set_non_blocking(state->listen_socket)) {
        return fail("could not set listen socket non-blocking");
    }

    auto ok = state->registry.register_command("version", "engine version string",
                                               &version_command, nullptr);
    if (ok) {
        ok = state->registry.register_command(
            "log.recent", "log.recent [N] - most recent N log entries (default 50, max 128)",
            &log_recent_command, nullptr);
    }
    if (ok) {
        ok = state->registry.register_command("mem.snapshot",
                                              "per-tag allocation statistics snapshot",
                                              &mem_snapshot_command, nullptr);
    }
    if (!ok) {
        return fail("built-in command registration failed");
    }

    HUE_LOG_INFO("debug channel listening on 127.0.0.1:%u", static_cast<unsigned>(state->port));

    DebugChannel channel;
    channel.m_state = state;
    return channel;
}

DebugChannel::DebugChannel(DebugChannel&& other) noexcept : m_state(other.m_state) {
    other.m_state = nullptr;
}

DebugChannel::~DebugChannel() {
    if (m_state == nullptr) {
        return;
    }
    if (m_state->client_socket != kInvalidSocket) {
        close_socket(m_state->client_socket);
    }
    if (m_state->listen_socket != kInvalidSocket) {
        close_socket(m_state->listen_socket);
    }
#if defined(_WIN32)
    if (m_state->winsock_initialized) {
        WSACleanup();
    }
#endif
    m_state->~State();
    const auto freed = heap_free(m_state);
    (void)freed;
    m_state = nullptr;
}

Result<void> DebugChannel::register_command(const char* name, const char* help, DebugCommandFn fn,
                                            void* user_data) noexcept {
    if (m_state == nullptr) {
        return ErrorCode::kInvalidArgument;
    }
    return m_state->registry.register_command(name, help, fn, user_data);
}

std::uint16_t DebugChannel::port() const noexcept {
    return m_state != nullptr ? m_state->port : 0;
}

bool DebugChannel::client_connected() const noexcept {
    return m_state != nullptr && m_state->client_socket != kInvalidSocket;
}

void DebugChannel::update() noexcept {
    if (m_state == nullptr) {
        return;
    }
    State& state = *m_state;

    if (state.client_socket == kInvalidSocket) {
        const SocketHandle accepted = accept(state.listen_socket, nullptr, nullptr);
        if (accepted == kInvalidSocket) {
            return; // nobody waiting (or transient error): try again next frame
        }
        if (!set_non_blocking(accepted)) {
            close_socket(accepted);
            return;
        }
        set_no_delay(accepted);
        state.client_socket = accepted;
        state.line_length = 0;
        state.discarding_long_line = false;
        HUE_LOG_INFO("debug channel client connected");
    }

    const auto drop_client = [&state](const char* reason) {
        close_socket(state.client_socket);
        state.client_socket = kInvalidSocket;
        state.line_length = 0;
        state.discarding_long_line = false;
        HUE_LOG_INFO("debug channel client disconnected (%s)", reason);
    };

    std::size_t commands_this_frame = 0;
    char incoming[2048];

    // Read while data is available, dispatching complete lines. The command
    // cap gates further reads, bounding per-frame work even against a
    // client that floods the socket.
    while (commands_this_frame < kDebugChannelMaxCommandsPerFrame) {
        const auto received = recv(state.client_socket, incoming,
#if defined(_WIN32)
                                   static_cast<int>(sizeof(incoming)),
#else
                                   sizeof(incoming),
#endif
                                   0);
        if (received == 0) {
            drop_client("closed by peer");
            return;
        }
        if (received < 0) {
            if (!last_error_would_block()) {
                drop_client("receive error");
                return;
            }
            return; // no more data this frame
        }

        for (std::size_t i = 0; i < static_cast<std::size_t>(received); ++i) {
            const char byte = incoming[i];
            if (byte != '\n') {
                if (state.discarding_long_line) {
                    continue;
                }
                if (state.line_length >= kDebugChannelMaxLine - 1) {
                    // Untrusted input over the cap: drop the rest of the
                    // line, answer with an error when it finally ends.
                    state.discarding_long_line = true;
                    continue;
                }
                state.line_buffer[state.line_length] = byte;
                ++state.line_length;
                continue;
            }

            DebugResponse response(state.response_buffer, kDebugChannelResponseCapacity);
            if (state.discarding_long_line) {
                state.discarding_long_line = false;
                state.line_length = 0;
                write_error_envelope(response, "command line too long");
            } else {
                if (state.line_length > 0 && state.line_buffer[state.line_length - 1] == '\r') {
                    --state.line_length;
                }
                state.line_buffer[state.line_length] = '\0';
                state.line_length = 0;
                state.registry.execute_line(state.line_buffer, response);
            }
            ++commands_this_frame;
            if (!send_all(state.client_socket, response.c_str(), response.length())) {
                drop_client("send failed");
                return;
            }
        }
    }
}

#endif // HUE_DEBUG_CHANNEL

} // namespace hue
