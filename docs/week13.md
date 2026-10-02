# Week 13 diagnostics and scene configuration

Engineering checkpoint, October 2, 2026. External acceptance is pending. The
owner authorized engineering milestones to proceed independently of the earlier
playtest gates; calendar dates no longer control implementation order.

## Implemented

F2 or controller Back cycles game, combat diagnostics, and tagged CPU memory.
Combat shows buffer expiry, event flags, activation IDs, last AI perception/LOS,
cooldowns and state age. Perception is the last simulated tick, not a new render
query. Memory shows per-tag live/peak bytes, allocations and per-frame requests;
these are not VRAM measurements. Both pages retain the fixed HUD capacity.

The executable loads `assets/config/arena.json` beside itself. The version 1
schema accepts exactly the shipped arena, Knight and Barbarian identifiers and
two spawn transforms. F5 validates the staged file; R applies accepted edits on
restart. Missing, truncated or invalid edits retain both the active scene and
any earlier valid pending scene. Startup with invalid required data fails.
Controllers, entities, models and draw bindings are reused.

The loader intentionally does not describe arbitrary environments. Spawns must
remain in the existing arena's obstacle-free south strip: x [-3,3], z [2,4.5],
y 0.05, at least one metre apart, yaw in [-180,180] degrees. This conservative
region excludes the pillar, ramp and ring boxes; changing arena geometry requires
revisiting the approved region. Asset identifiers cannot become filesystem paths.

The JSON subset is capped at 4 KiB, 32 fields, 47-byte ASCII keys/strings and
four numbers per array. Unknown/duplicate keys, nonfinite numbers, nesting,
escapes, extra tokens and unsupported versions are rejected. No allocations or
file accesses occur in the memory-only parser.

## Verification

- Windows MSVC Release: 144/144 CTest tests passed, no skips, 92.00 seconds.
- Windows MSVC ASan: 144/144 passed, no skips, 127.69 seconds.
- Tests cover malformed syntax, every scene truncation, references, spawn bounds,
  overlapping spawns, pending-state preservation, twenty scene restarts, and both
  bounded HUD pages. Real Vulkan/reset/debug-channel smoke tests passed.
- Windows Clang 22 ASan/libFuzzer: 10,720,578 scene executions in 61 seconds,
  exit 0; no reported sanitizer finding. Seeds are tracked, generated corpus and
  logs are ignored under `build/week13-*`.
- Windows 11 / RTX 5090, 1280x720: the live ready screen, combat diagnostics and
  memory page were inspected through keyboard input. This is developer QA, not
  physical gamepad or unaided external usability acceptance.
- The new Linux CI fuzz target is registered for 60 seconds; it has not run here.

Reproduce with `cmake --build --preset windows-release` and
`ctest --preset windows-release`; repeat with `windows-asan`. Fuzz scene data
using the `fuzz_scene` target in the existing sanitized Clang fuzz configuration.

## Acceptance still pending

Earlier external playtests, physical gamepad and resize checks, Linux CI, and a
saved Tracy capture remain open. The sustained fuzz campaign is separate. This
checkpoint does not claim the entire Week 13 DoD or release readiness.
