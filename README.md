# Hue

> A from-scratch C++20/Vulkan third-person combat runtime: custom memory systems, skeletal animation, ECS, Jolt physics, secure fuzz-tested asset loading, runtime combat debug tools, and a playable enemy encounter.

Built in 20 weeks, in public, before GTA 6 ships (Nov 19, 2026).

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
  /ecs         # sparse-set ECS
  /physics     # jolt integration layer
  /asset       # gltf import (mesh+skin+anim), shader compile, hot reload
  /debugui     # imgui overlays: combat state, hitboxes, AI, stats
/game          # combat gameplay code (character, enemy AI, encounter)
/tests         # doctest suites
/tools         # scripts: shader compile, fuzz harnesses (/tools/fuzz)
/docs          # public timeline, devlog drafts, threat-model.md
```

See [docs/timeline.md](docs/timeline.md) for the week-by-week development timeline.
