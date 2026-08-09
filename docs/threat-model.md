# Hue — Threat Model (draft)

Started Week 5. This is a living document: each new trust boundary gets a
section as the subsystem lands. Week 18 publishes the full retrospective.

## Scope

Hue is a local desktop game runtime. There is no network multiplayer and no
remote asset store. The threat surface that matters today is **untrusted
bytes on disk** that the engine parses, plus a few opt-in local debug
surfaces that never leave the machine.

Assumptions:

- The player (or a content pipeline) can place arbitrary files next to the
  game and ask the engine to load them.
- CI and developers run fuzzers and sanitizers against those parsers.
- The localhost debug channel and MCP bridge are developer-only, disabled
  in Release by default, and never bind outside `127.0.0.1`.

Non-goals for now: sandboxing the process from the OS, DRM, anti-cheat,
remote code execution via gameplay netcode.

## Assets as untrusted input

| Asset | Entry point | Status |
|-------|-------------|--------|
| glTF / GLB meshes | `hue::asset::load_gltf` | **Week 5** — validated, fuzzed |
| SPIR-V shaders (reload) | `hue::validate_spirv_bytes` | Week 4 boundary check; full reflection hardening in Week 14 |
| Textures / images | (not yet) | Week 6 |
| Skinned / animated glTF | (not yet) | Weeks 6–7 |
| Scene JSON | (not yet) | Week 12 |
| Combat tuning JSON | (not yet) | Week 14 |

### glTF loader trust boundary (Week 5)

Attacker goal: crash the process, hang it, or trick the GPU into reading
out-of-range vertex data (which on some drivers is a denial-of-service or
worse).

Defenses, in order:

1. **Hard size caps** — file ≤ 64 MiB; vertices / indices / primitives /
   instances capped (`kGltfMax*` in `gltf_loader.h`).
2. **Memory-only parse** — `cgltf_parse` + `cgltf_load_buffers(..., nullptr)`.
   Embedded GLB BIN chunks and data URIs resolve; external `.bin` / HTTP
   URIs are rejected (`kUnsupported`) so the fuzz target never touches disk.
3. **`cgltf_validate`** — accessor offsets and counts checked against
   buffer sizes before we read a single attribute.
4. **Format allow-list** — triangles only; float32 positions/normals/UVs;
   no sparse accessors; unknown topology → `kUnsupported`.
5. **Per-index bounds check** — every index must be `< vertex_count` for
   its primitive before the mesh is handed to the renderer.
6. **Tagged heap** — parser allocations go through `MemoryTag::kAssets`
   with canaries in Debug/Sanitized builds.

Fuzz entry point: `tools/fuzz/fuzz_gltf.cpp` → `load_gltf`. Seed corpus
lives in `tools/fuzz/corpus/gltf`. CI runs a timed libFuzzer pass on
Linux Clang + ASan/UBSan (see `.github/workflows/ci.yml`).

## Local debug surfaces

| Surface | Binding | Guards |
|---------|---------|--------|
| Debug command channel | `127.0.0.1` TCP, opt-in `--debug-channel` | Line/arg/command caps; compiled out when `HUE_DEBUG_CHANNEL=OFF`; Release builds keep the flag off by default |
| MCP bridge (`tools/mcp`) | stdio → localhost TCP | Same channel; no new socket |

These are not remote attack surfaces. They still treat every command line
as untrusted text (bounded buffers, no `std::string` growth from input).

## Deferred

- Texture decode (dimensions, mip counts, format abuse) — Week 6
- Skin / animation clip validation — Weeks 6–7
- Scene description JSON (capped counts, validated references) + dedicated
  fuzz harness — Week 12
- Shader hot-reload reflection validation + fuzz — Week 14
- Extended 24–48h fuzz campaign, crash taxonomy, published model — Week 18
