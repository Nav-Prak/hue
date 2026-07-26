#!/usr/bin/env python3
"""End-to-end smoke test for the Hue debug channel.

Spawns the game with --debug-channel, connects over TCP, exercises every
built-in command plus error handling, then asks the game to quit and
verifies a clean exit. Usage: debug_channel_smoke.py <path-to-hue-game>
"""

import json
import socket
import subprocess
import sys
import time

PORT = 46655  # non-default so a developer's live session is never hit
CONNECT_TIMEOUT_S = 20.0


def fail(proc, message):
    proc.kill()
    out, err = proc.communicate(timeout=10)
    print(f"FAIL: {message}", file=sys.stderr)
    print(f"game stdout:\n{out.decode(errors='replace')}", file=sys.stderr)
    print(f"game stderr:\n{err.decode(errors='replace')}", file=sys.stderr)
    sys.exit(1)


def connect_with_retry(proc):
    deadline = time.monotonic() + CONNECT_TIMEOUT_S
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            fail(proc, f"game exited early with code {proc.returncode}")
        try:
            return socket.create_connection(("127.0.0.1", PORT), timeout=2.0)
        except OSError:
            time.sleep(0.25)
    fail(proc, "could not connect to the debug channel")


def send_command(sock, command):
    sock.sendall((command + "\n").encode())
    buffer = b""
    sock.settimeout(10.0)
    while not buffer.endswith(b"\n"):
        chunk = sock.recv(65536)
        if not chunk:
            raise ConnectionError("engine closed the connection")
        buffer += chunk
    return json.loads(buffer.decode())


def main():
    game = sys.argv[1]
    # No --frames cap: with no renderer the loop spins at thousands of fps
    # and any cap is gone before we can connect. Lifetime is controlled by
    # the quit command; every failure path kills the process.
    proc = subprocess.Popen(
        [game, "--debug-channel", str(PORT)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        run_checks(proc)
    except SystemExit:
        raise
    except Exception as exc:  # any unexpected error must not leak the game process
        fail(proc, f"unexpected error: {exc!r}")


def run_checks(proc):
    sock = connect_with_retry(proc)

    try:
        reply = send_command(sock, "version")
        assert reply["ok"], reply
        assert "hue" in reply["data"]["version"].lower(), reply

        reply = send_command(sock, "status")
        assert reply["ok"], reply
        assert reply["data"]["frames"] >= 0, reply
        assert "uptime_seconds" in reply["data"], reply

        reply = send_command(sock, "log.recent 10")
        assert reply["ok"], reply
        entries = reply["data"]["entries"]
        assert len(entries) > 0, "expected log entries"
        assert any("debug channel" in e["message"] for e in entries), entries

        reply = send_command(sock, "mem.snapshot")
        assert reply["ok"], reply
        tags = {t["tag"]: t for t in reply["data"]["tags"]}
        assert "core" in tags, tags
        assert tags["core"]["current_bytes"] > 0, tags  # channel state itself is tagged core

        reply = send_command(sock, "help")
        assert reply["ok"], reply
        names = {c["name"] for c in reply["data"]["commands"]}
        assert {"help", "version", "status", "quit", "log.recent", "mem.snapshot"} <= names, names

        reply = send_command(sock, "no.such.command")
        assert not reply["ok"], reply
        assert "unknown command" in reply["error"], reply

        # Overlong line: must answer with an error, not crash or hang.
        reply = send_command(sock, "x" * 8192)
        assert not reply["ok"], reply
        assert "too long" in reply["error"], reply

        reply = send_command(sock, "quit")
        assert reply["ok"], reply
        assert reply["data"]["quitting"] is True, reply
    except (AssertionError, ConnectionError, json.JSONDecodeError, socket.timeout) as exc:
        fail(proc, f"command exchange failed: {exc}")
    finally:
        sock.close()

    try:
        code = proc.wait(timeout=20)
    except subprocess.TimeoutExpired:
        fail(proc, "game did not exit after quit command")
    if code != 0:
        fail(proc, f"game exited with code {code} after quit")

    print("debug channel smoke test passed")


if __name__ == "__main__":
    main()
