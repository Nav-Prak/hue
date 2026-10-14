# Hue

> A from-scratch C++20/Vulkan third-person combat runtime: custom memory systems, skeletal animation, ECS, Jolt physics, bounded asset loading with fuzz coverage, runtime combat debug tools, and a playable enemy encounter.

The two-attack Windows v1 implementation is frozen as a local candidate. On
October 14, 2026 the owner waived external acceptance gates to finish this scope
quickly; unperformed checks are not claimed as passed. See
[candidate scope, verification and risks](docs/release-candidate.md). The engine integrates
fixed-tick movement, animation, combat, enemy AI, start/results/restart,
combat/memory diagnostics, staged scene/tuning edits, validated shader reload,
per-fight telemetry and local review packaging. See the [package verification notes](docs/week16.md) and
[current baseline and revised plan](docs/timeline.md) for implementation versus
acceptance status.

## Playing the encounter

Run `build/windows-msvc/game/Release/hue.exe` after building (Linux:
`build/linux-clang/game/hue`). Models/events and shaders stage beside the executable.

| Action | Keyboard / mouse | Gamepad (Xbox-style mapping) |
|---|---|---|
| Start; return to ready after a result | Enter | Start |
| Reset to ready at any time | R | Start after a result |
| Move; run | WASD; Shift | Left stick, analog speed |
| Orbit camera | Hold RMB and move mouse | Right stick |
| Light / heavy attack | LMB / E | X / Y |
| Dodge | Space | B |
| Toggle lock-on | Tab | Right-stick click |
| Cycle game, combat and memory diagnostics | F2 | Back |
| Stage scene / tuning edits; apply on reset | F5 / F6; R | — |
| Debug fly camera; quit | F1; Escape | — |

Attack windows and dodge invulnerability follow clip events. Simultaneous lethal
hits trade and resolve to player defeat. The result stops gameplay while death
animation continues. Restart reuses loaded assets and controllers.

`hue --encounter-smoke --require-renderer` runs 20 scripted reset/outcome cycles
with real rendering. It injects lethal damage to exercise both results and is
not a substitute for a human playtest or a combat-balance evaluation.

## Building

Requirements: CMake ≥ 3.28, a C++20 compiler (MSVC 2022+ or Clang), Ninja on Linux.

```
cmake --preset windows-msvc      # or linux-clang
cmake --build --preset windows-release
ctest --preset windows-release
```

Sanitized build (ASan on MSVC, ASan+UBSan on Clang):

```
cmake --preset windows-msvc-asan   # or linux-clang-sanitized
cmake --build --preset windows-asan
ctest --preset windows-asan
```

## Local review package

Windows packaging requires Python 3.11+ and the existing Release build toolchain.
Choose a fresh output directory; the tool refuses to overwrite a package:

```sh
python tools/package.py --build build/windows-msvc --output build/packages/hue-review --require-clean
python tools/package.py --verify build/packages/hue-review
python tests/package_smoke.py build/packages/hue-review
```

The adjacent ZIP includes the executable, seven compiled shaders, models/events,
scene/tuning data, MSVC Release runtime DLLs, controls and dependency notices.
The manifest records file hashes and source revision/dirty state. Extract the
whole ZIP, then run `hue.exe`; no compiler or Vulkan SDK is needed on the target,
but Windows x64, SSE4.2 and a Vulkan 1.3 graphics driver are required.

Capture optional local reports with `hue.exe --telemetry C:/writable/fights.jsonl`;
summarize with `python tools/fight_report.py C:/writable/fights.jsonl`. See
[telemetry and evidence limits](docs/week15.md). Runtime files and config are
executable-relative, independent of the launch directory.

This is a local candidate with owner-waived external gates, not an externally
verified release. Playtests, physical gamepad, clean-machine and Linux checks
have not been completed. Sustained fuzzing, interactive balance evidence and saved
Tracy profiling are deferred; local checks and runtime validation remain enabled.
Hue has no declared project-wide
license; dependency/asset notices do not choose one for it. Nothing is published
by these commands.

## Layout

```
/engine
  /core        # platform, memory, math, containers, jobs, log
  /render      # vulkan backend, passes, materials, skinning
  /anim        # skeletal animation runtime: sampling, blending, events
  /combat      # shared combat logic, hit volumes, enemy AI
  /ecs         # sparse-set ECS
  /physics     # jolt integration layer
  /asset       # gltf import (mesh+skin+anim), shader compile, hot reload
/game          # character/encounter lifecycle, bitmap HUD and combat diagnostics
/tests         # doctest suites
/tools         # scripts: shader compile, fuzz harnesses (/tools/fuzz)
/docs          # public timeline, devlog drafts, threat-model.md
```

See [docs/timeline.md](docs/timeline.md) for the week-by-week development timeline and [docs/threat-model.md](docs/threat-model.md) for the living threat model.
