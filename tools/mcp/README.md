# Hue MCP Server

Exposes a running Hue engine to AI agents (Cursor, Claude Desktop, or any
MCP client) through the engine's localhost debug channel.

```
AI agent  --MCP/stdio-->  hue_mcp.py  --TCP 127.0.0.1:46600-->  hue.exe --debug-channel
```

The engine side is a plain text-command protocol (one JSON object per line
back), so nothing AI-specific lives in engine code. Systems register new
debug commands as they land; the `run_command` passthrough reaches all of
them without touching this server.

## Setup

```sh
pip install -r requirements.txt
```

Start the game with the channel enabled (it is off by default, localhost
only, and compiled out entirely when `HUE_DEBUG_CHANNEL=OFF`):

```sh
hue.exe --debug-channel          # default port 46600
hue.exe --debug-channel 46601    # explicit port
```

## Cursor configuration

Add to `.cursor/mcp.json` (project) or `~/.cursor/mcp.json` (global):

```json
{
  "mcpServers": {
    "hue-engine": {
      "command": "python",
      "args": ["C:/ProjectHue/tools/mcp/hue_mcp.py"],
      "env": { "HUE_DEBUG_PORT": "46600" }
    }
  }
}
```

## Tools

| Tool | Engine command | Purpose |
| --- | --- | --- |
| `engine_status` | `status` | frames, sim steps, uptime |
| `recent_logs` | `log.recent N` | ring-buffer log entries |
| `memory_snapshot` | `mem.snapshot` | per-tag allocator statistics |
| `list_commands` | `help` | discover everything registered |
| `run_command` | any | generic passthrough (`quit`, future `entity.list`, ...) |

## Manual poke (no MCP client needed)

```sh
python -c "import socket; s=socket.create_connection(('127.0.0.1',46600)); s.sendall(b'status\n'); print(s.recv(65536).decode())"
```
