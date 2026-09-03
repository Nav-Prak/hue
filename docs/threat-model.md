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
| Textures / images (PNG/JPEG) | `load_gltf` / `load_gltf_skinned` | **Week 6** — decoded via stb_image behind caps, fuzzed |
| Skinned / animated glTF | `hue::asset::load_gltf_skinned` | **Weeks 6–7** — skeleton/clip validation, fuzzed, bounded sampling + GPU palettes |
| Animation event JSON | `hue::anim::parse_animation_events` | **Week 7** — 64 KiB/256-event caps, strict schema, clip/time validation |
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

Fuzz entry point: `tools/fuzz/fuzz_gltf.cpp` → `load_gltf` **and**
`load_gltf_skinned` (every input runs through both). Seed corpus lives in
`tools/fuzz/corpus/gltf` and includes skinned and textured samples. CI
runs a timed libFuzzer pass on Linux Clang + ASan/UBSan (see
`.github/workflows/ci.yml`).

### Texture decode boundary (Week 6)

Attacker goal: memory corruption inside the image decoder (historically
the richest bug class in asset pipelines), or memory exhaustion via
decompression bombs.

Defenses:

1. **Embedded-only images** — only buffer-view images load; URI-referenced
   images (file paths or base64) are rejected (`kUnsupported`), so the
   image path inherits the memory-only guarantee.
2. **Restricted decoder build** — stb_image compiles with `STBI_ONLY_PNG`
   + `STBI_ONLY_JPEG` + `STBI_NO_STDIO`; every other format's code path
   does not exist in the binary.
3. **Dimension cap in the decoder** — `STBI_MAX_DIMENSIONS 4096` rejects
   oversized images before allocation, and the loader re-checks against
   `kGltfMaxTextureDim`.
4. **Decoded-byte budget** — total decoded RGBA across a file is capped
   (`kGltfMaxDecodedTextureBytes`, checked with overflow-safe math), so a
   64 MiB file cannot expand into gigabytes of pixels.
5. **Tagged, canaried allocations** — stb allocations route through the
   engine heap (`MemoryTag::kAssets`), so decode buffers get the same
   canary/poison treatment as everything else and show up in memory
   snapshots.
6. **Texture count / material caps** — `kGltfMaxTextures`,
   `kGltfMaxMaterials`; factors are clamped to [0, 1] and texture indices
   bounds-checked.

### Skin and animation clip boundary (Week 6)

Attacker goal: out-of-range joint indices (GPU skinning reads a joint
matrix array in Week 7 — an unchecked index is an out-of-bounds read on
every vertex, every frame), NaN poisoning of pose math, or hangs via
degenerate keyframe data.

Defenses:

1. **Joint caps and bounds** — ≤ `kGltfMaxJoints` (256) joints; every
   `JOINTS_0` value is checked `< joint_count` before remapping. Weights
   must be finite and non-negative and are renormalized to sum 1.
2. **Topological reorder with cycle detection** — joints are stored
   parent-before-child; a cyclic "hierarchy" is rejected instead of
   hanging pose propagation.
3. **Inverse bind + rest pose finiteness** — every matrix and TRS
   component must be finite; matrix-transform joints are rejected
   (`kUnsupported`) so Week 7 blending always has a valid TRS rest pose.
4. **Keyframe track validation** — times finite, non-negative,
   non-decreasing, ≤ `kGltfMaxClipSeconds`; key counts ≤
   `kGltfMaxKeyframes`; output counts must match input counts; LINEAR
   interpolation only; rotation keys must not be zero-length (they get
   normalized at sampling time).
5. **Runtime revalidation** — clip time and blend parameters are finite and
   clamped; channel targets and topological parent order are checked before
   pose propagation; renderer uploads require an exact joint-count match.
6. **Bounded event sidecars** — parser accepts only the `events` schema,
   caps file/event/string sizes, rejects non-finite or negative times, then
   verifies every event against a real clip and its duration.

### Physics collision cooking boundary (Week 8)

Static collision is cooked from the same mesh data the renderer draws
(procedural arena and validated glTF imports), so asset-derived triangles
reach Jolt's `MeshShape` cooker.

Defenses:

1. **Upstream validation** — cooked data has already passed the glTF
   loader's accessor/index checks; the procedural scene is trusted code.
2. **Facade revalidation** — `add_static_mesh` re-checks every index
   against the vertex count, requires index triples, and caps soup size
   (≤ 1M vertices / 3M indices) before Jolt sees it.
3. **Bounded world** — static body count is capped; cook failures reject
   the mesh with an error instead of asserting inside Jolt.

Capsule controllers and raycasts only consume engine-generated parameters
(validated at the facade), not asset data.

## Local debug surfaces

| Surface | Binding | Guards |
|---------|---------|--------|
| Debug command channel | `127.0.0.1` TCP, opt-in `--debug-channel` | Line/arg/command caps; compiled out when `HUE_DEBUG_CHANNEL=OFF`; Release builds keep the flag off by default |
| MCP bridge (`tools/mcp`) | stdio → localhost TCP | Same channel; no new socket |

These are not remote attack surfaces. They still treat every command line
as untrusted text (bounded buffers, no `std::string` growth from input).

## Deferred

- Scene description JSON (capped counts, validated references) + dedicated
  fuzz harness — Week 12
- Shader hot-reload reflection validation + fuzz — Week 14
- Extended 24–48h fuzz campaign, crash taxonomy, published model — Week 18
