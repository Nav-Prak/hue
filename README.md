# Hue

> A from-scratch C++20/Vulkan third-person combat runtime: custom memory systems, skeletal animation, ECS, Jolt physics, secure fuzz-tested asset loading, runtime combat debug tools, and a playable enemy encounter.

Building in public toward November 19, 2026. The engine now integrates
fixed-tick movement, animation, combat, enemy AI, start/results/restart, and a
minimal combat diagnostics overlay. See the [Week 12 verification notes](docs/week12.md) and
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
| Toggle combat diagnostics | F2 | Back |
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
  /debugui     # planned Week 13: expanded tools; minimal overlay is in /game
/game          # character/encounter lifecycle, bitmap HUD and combat diagnostics
/tests         # doctest suites
/tools         # scripts: shader compile, fuzz harnesses (/tools/fuzz)
/docs          # public timeline, devlog drafts, threat-model.md
```

See [docs/timeline.md](docs/timeline.md) for the week-by-week development timeline and [docs/threat-model.md](docs/threat-model.md) for the living threat model.
