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
| SPIR-V shaders (reload) | `hue::validate_shader_contract` | Week 14 semantic validator and fixed-ABI checks; CPU-only fuzz target |
| Textures / images (PNG/JPEG) | `load_gltf` / `load_gltf_skinned` | **Week 6** — decoded via stb_image behind caps, fuzzed |
| Skinned / animated glTF | `hue::asset::load_gltf_skinned` | **Weeks 6–7** — skeleton/clip validation, fuzzed, bounded sampling + GPU palettes |
| Animation event JSON | `hue::anim::parse_animation_events` | **Weeks 7/12** — 64 KiB/256-event caps, strict schema, clip/time validation; dedicated parser/semantic-validation fuzz target registered in CI |
| Scene JSON | `parse_scene` | Week 13 bounded schema and reset-boundary staging |
| Combat tuning JSON | `parse_tuning` | Week 14 bounded schema, clip checks, reset-boundary staging |

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

Week 12 adds `tools/fuzz/fuzz_animation_events.cpp`: memory-only parsing followed
by semantic validation against a small fixed clip set. Seeds live in
`tools/fuzz/corpus/animation_events`; Linux CI schedules 60 seconds under
ASan/UBSan. Local Windows Clang/ASan smoke results and limits are recorded in
[the Week 12 notes](week12.md), not presented as a Linux CI result.

The game copies sampled global/skinning matrices into fixed-capacity actor-owned
storage before resetting scratch memory. Zero-simulation-tick and hitstop frames
therefore do not retain expired frame-arena pointers. This ownership regression
is tested separately from parser fuzzing.

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

### Combat overlap queries (Week 10)

Hitboxes and hurtboxes are engine-authored capsules/spheres attached to
already-imported joints. They are **not** cooked from mesh triangles.

Defenses:

1. **Bounded extents** — radius and capsule half-height must be finite,
   positive (radius), and ≤ `kMaxCombatShapeExtent` (8 m). Oversized or
   NaN arguments miss instead of creating a Jolt shape that could hang
   narrow-phase.
2. **Finite transforms** — every matrix/center handed to
   `overlap_capsules` / `overlap_capsule_sphere` / `overlap_spheres` is
   checked finite before `CollisionDispatch`.
3. **Joint bind is name lookup** — unknown aliases are skipped; a
   character with zero hitboxes or hurtboxes fails initialize rather than
   querying joint index `-1`.

## Local debug surfaces

| Surface | Binding | Guards |
|---------|---------|--------|
| Debug command channel | `127.0.0.1` TCP, opt-in `--debug-channel` | Line/arg/command caps; compiled out when `HUE_DEBUG_CHANNEL=OFF`; Release builds keep the flag off by default |
| MCP bridge (`tools/mcp`) | stdio → localhost TCP | Same channel; no new socket |

These are not remote attack surfaces. They still treat every command line
as untrusted text (bounded buffers, no `std::string` growth from input).

## Scene configuration

The game reads an executable-relative `assets/config/arena.json` at startup and
on F5. `game/src/config_json.cpp` is a 4 KiB, allocation-free flat JSON subset:
32 fields, 47-byte ASCII keys/strings, four numeric elements per array; duplicate
keys, malformed number grammar, nonfinite values, escapes/nesting and trailing
data fail. `scene_config.cpp` requires the exact versioned schema and approved
arena/rig IDs, never file paths. Two finite, separated spawns are confined to the
existing arena's clear staging strip. This is not a general scene deserializer.

Validation precedes staging. An invalid candidate cannot alter the live or
pending scene. Accepted transforms apply on reset with existing resources; no
new bodies/assets/entities accumulate. Required startup files fail closed.
Tests exercise truncation, invalid references/geometry and transactional staging.
`fuzz_scene` is memory-only with seeds and a 60-second CI invocation. See
[actual local verification](week13.md); registered CI is not evidence of a CI run.

## Remaining deferred boundaries

- Extended 24–48h fuzz campaign, crash taxonomy, published model — Week 18

## Local reports and packaging

Fight telemetry is opt-in local output, not a network service. A fixed-buffer
formatter emits one schema-versioned line per completed fight; failed writes
disable recording without stopping combat. The offline Python report reader caps
line/file/record counts, validates counters and finite timings, rejects incomplete
records, and excludes automated/injected samples by default. It never evaluates
input. See [Week 15](week15.md) for tests and evidence limits.

The package builder installs an explicit runtime component, not arbitrary source
directories, into a fresh destination. Its standard-library ZIP is a distribution
container, not a new engine pack parser. Manifest verification inventories actual
files (bounded count/bytes, no symlinks), checks required runtime inputs/notices
and hashes; it never follows paths supplied by manifest keys. Hashes are not
signatures and do not establish executable trust. The smoke tool launches only
the explicitly selected local review package, so do not use it to inspect an
untrusted download. [Week 16](week16.md) records package and platform limitations.

## Tuning and shader reload (Week 14)

Tuning uses the same capped JSON subset as scenes, an exact versioned schema,
finite/ranged values and cross-field constraints. Both real actors validate
scaled cancel events against their original clip times before staging; reset
rebinds the animator before editing event storage. Invalid edits preserve active
and pending data. Original event times prevent repeated reload scaling drift.

Runtime SPIR-V is capped at 128 KiB and 4096 IDs, semantically checked by the
already-pinned SPIRV-Tools validator (runtime use approved by the owner), then
checked against seven fixed shader interfaces. Entry stage, locations/types,
descriptor sets/bindings, block offsets/strides and push constants must match.
The exact checked byte buffer reaches Vulkan. Replacement pipelines are built
first; old resources remain until a successful device-idle wait. Failure destroys
only the candidate. Live handle counters cover modules, layouts and pipelines.

This restricted interface check is not a general shader sandbox or a guarantee
about driver bugs or shader execution time. SPIRV-Tools uses its own transient
allocations. CPU parser fuzzing never submits untrusted generated modules to a
GPU; live reload tests use authored good, truncated and wrong-interface modules.
`fuzz_tuning` and `fuzz_shader_contract` have corpus and CI smoke registration;
[Week 14](week14.md) distinguishes measured local results from pending runs.
