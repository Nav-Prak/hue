#!/usr/bin/env python3
"""MCP server exposing a running Hue engine's debug channel to AI agents.

The engine must be started with --debug-channel (see tools/mcp/README.md).
Each tool opens a short-lived TCP connection to the engine's localhost
debug port, sends one command line, and returns the engine's JSON reply.
Per-call connections keep the engine's single client slot free between
requests, so a human console session and an agent can interleave.
"""

import json
import os
import socket

from mcp.server.fastmcp import FastMCP

HOST = os.environ.get("HUE_DEBUG_HOST", "127.0.0.1")
PORT = int(os.environ.get("HUE_DEBUG_PORT", "46600"))

mcp = FastMCP(
    "hue-engine",
    instructions=(
        "Tools for inspecting and controlling a running Hue engine instance. "
        "The game must be launched with the --debug-channel flag. Use "
        "list_commands to discover everything the engine currently exposes; "
        "run_command executes any of them."
    ),
)


def _exchange(command: str) -> str:
    """Send one command line, return the engine's JSON response line."""
    try:
        with socket.create_connection((HOST, PORT), timeout=5.0) as sock:
            sock.sendall((command.strip() + "\n").encode())
            sock.settimeout(10.0)
            buffer = b""
            while not buffer.endswith(b"\n"):
                chunk = sock.recv(65536)
                if not chunk:
                    break  # engine closed; fall through with whatever arrived
                buffer += chunk
    except OSError as exc:
        return json.dumps(
            {
                "ok": False,
                "error": (
                    f"could not reach the engine at {HOST}:{PORT} ({exc}). "
                    "Is the game running with --debug-channel?"
                ),
            }
        )
    return buffer.decode(errors="replace").strip()


@mcp.tool()
def run_command(command: str) -> str:
    """Run any registered engine debug command (e.g. 'status', 'log.recent 20').

    Use list_commands to see what is available; systems register new
    commands as the engine grows, and this passthrough reaches all of them.
    """
    return _exchange(command)


@mcp.tool()
def list_commands() -> str:
    """List every debug command the running engine exposes, with help text."""
    return _exchange("help")


@mcp.tool()
def engine_status() -> str:
    """Frames rendered, fixed-timestep sim steps, and uptime of the running game."""
    return _exchange("status")


@mcp.tool()
def recent_logs(count: int = 50) -> str:
    """The engine's most recent log entries (leveled ring buffer), oldest first.

    Args:
        count: number of entries to fetch (max 128).
    """
    return _exchange(f"log.recent {int(count)}")


@mcp.tool()
def memory_snapshot() -> str:
    """Per-tag allocator statistics: current/peak bytes and live allocation counts."""
    return _exchange("mem.snapshot")


if __name__ == "__main__":
    mcp.run()
