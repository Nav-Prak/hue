# Hue

A third-person combat game runtime written from scratch in C++20 and Vulkan,
built in 20 weeks before GTA 6 ships.

![The gauntlet scene: the player faces three enemies among box props, with sun shadows and the combat HUD](docs/media/gauntlet.png)

*The `gauntlet` scene: one player against three enemies, shadow-mapped sun,
static props with collision. Characters and animations are KayKit (CC0).*

## What's in it

- **Engine core.** A tagged heap, frame arenas and pool allocators that
  account for every engine allocation, a sparse-set ECS, a job system, and no exceptions or RTTI
  in engine code.
- **Rendering.** A Vulkan 1.3 renderer with GPU skinning, a lit pass, a 2048²
  shadow map with PCF for the sun, point lights, and shader hot reload that
  validates a new shader before swapping it in.
- **Animation and combat.** Skeletal animation sampling and blending, clip
  events that drive attack windows and dodge invulnerability, light and heavy
  attacks, lock-on with target switching, and enemy AI for one player against
  up to seven enemies.
- **Physics and audio.** Jolt character controllers and static collision;
  twelve procedural sound effects with distance attenuation and panning,
  through Hue's own WAV parser.
- **Data-driven scenes.** Actors, props, lights and the sun come from
  validated scene files that can be swapped at runtime.
- **Security-minded loading.** Every byte read from a file is treated as
  untrusted. The glTF, animation-event, scene, tuning, shader-contract and WAV
  parsers are bounded, validated and fuzzed under ASan and UBSan. Fuzzing
  found two real bugs in third-party parsing code (misaligned glTF accessors
  in cgltf, a null zero-length `memcpy` in stb_image); both are fixed with
  regression tests, and a 24-target-hour campaign afterwards found nothing.
  See the [threat model](docs/threat-model.md) and
  [fuzz campaign log](docs/fuzz-campaign.md#run-log).
- **Tooling.** In-game combat and memory diagnostics, Tracy profiling, per-fight
  telemetry, and a packager that produces a verified, hash-manifested ZIP.

186 automated tests pass in CI on Windows and Linux, and 185 under
AddressSanitizer (plus UBSan on Linux).
What has and has not been checked is listed under [Status](#status).

## Download and play

Download `hue-v1.0.0-windows-x64.zip` from the
[latest release](https://github.com/Nav-Prak/hue/releases/latest), extract the
whole archive and run `hue.exe`. You need 64-bit Windows and a graphics driver
with Vulkan 1.3 (any recent NVIDIA, AMD or Intel driver). Nothing else to
install. `hue.exe --scene gauntlet` starts the three-enemy fight.

To run your own build instead, use `build/windows-msvc/game/Release/hue.exe`
(Linux: `build/linux-clang/game/hue`); models, shaders and config are staged
beside the executable.

## Controls

| Action | Keyboard / mouse | Gamepad (Xbox-style mapping) |
|---|---|---|
| Start; return to ready after a result | Enter | Start |
| Reset to ready at any time | R | Start after a result |
| Move; run | WASD; Shift | Left stick, analog speed |
| Orbit camera | Hold RMB and move mouse | Right stick |
| Light / heavy attack | LMB / E | X / Y |
| Dodge | Space | B |
| Toggle lock-on | Tab | Right-stick click |
| Next lock-on target | Q | Right bumper |
| Cycle game, combat and memory diagnostics | F2 | Back |
| Stage scene / tuning edits; apply on reset | F5 / F6; R | — |
| Stage the next shipped scene; mute | F7; M | — |
| Debug fly camera; quit | F1; Escape | — |

Attack windows and dodge invulnerability follow clip events. Simultaneous lethal
hits trade and resolve to player defeat. The result stops gameplay while death
animation continues. Restart reuses loaded assets and controllers.

Scenes live in `assets/config` and are chosen by name: `hue --scene gauntlet`
starts the three-enemy scene with props. A scene file lists the player and up to
seven enemies, static box props, point lights and the sun; see
[the scene format and its validation](docs/threat-model.md#scene-configuration).
`--no-audio` runs without opening an output device.

`hue --encounter-smoke --require-renderer` runs 20 scripted reset/outcome cycles
with real rendering. It injects lethal damage to exercise both results and is
not a substitute for a human playtest or a combat-balance evaluation.

## Building

Requirements: Git, CMake ≥ 3.28 and a C++20 compiler (Visual Studio 2022 or
later with the C++ workload on Windows; Clang and Ninja on Linux, plus
`xorg-dev` on Ubuntu). The Vulkan SDK is not needed: every dependency is
fetched and built at configure time, so the first configure needs internet
access and takes a few minutes.

```
git clone https://github.com/Nav-Prak/hue.git
cd hue
cmake --preset windows-msvc      # or linux-clang
cmake --build --preset windows-release   # or linux-release
ctest --preset windows-release           # or linux-release
```

Sanitized build (ASan on MSVC, ASan+UBSan on Clang):

```
cmake --preset windows-msvc-asan   # or linux-clang-sanitized
cmake --build --preset windows-asan
ctest --preset windows-asan
```

## Making a game with Hue

Hue is MIT-licensed, so you can build your own game on it, open or closed
source, commercial or not. Keep the copyright notice in `LICENSE` with the
engine code, and ship `third-party-notices.txt` and `licenses/` with your game
(the packager below does that for you).

**1. Change the fight without writing code.** Everything in
`assets/config` is data and is validated when it loads:

- `tuning.json` holds the combat numbers: movement and dodge speeds, health,
  poise and its regeneration, damage and timing of light and heavy attacks, AI
  ranges, speeds and reaction times, lock-on range.
- A scene file sets the sun, ambient light, up to 4 point lights, up to 16 box
  props and the actors: one player first, then 1–7 enemies, each a
  `kaykit_knight` or `kaykit_barbarian` with a position and facing. Copy
  `gauntlet.json` to `assets/config/my_arena.json`, rebuild (config is staged
  beside the executable), and run `hue --scene my_arena`.
- Iterate live: edit the staged copy under
  `build/windows-msvc/game/Release/assets/config/`, press F5 (scene) or F6
  (tuning) in game to validate it, then R to restart with it. A bad edit is
  rejected and the working data is kept. Copy good edits back to
  `assets/config` so the next build keeps them.

The exact formats and limits are in the
[threat model](docs/threat-model.md#scene-configuration).

**2. Change how it looks.** Shaders are GLSL in `assets/shaders` and are
compiled to SPIR-V by the build. Start the game with
`hue --debug-channel 7777`, rebuild the shaders, and send the line
`shader.reload` to `127.0.0.1:7777` over TCP; the new shaders are validated
and swapped in without a restart, or rejected with the old ones kept.

**3. Write your own game.** The engine is a set of static libraries:
`hue::core` (memory, math, containers, jobs, logging), `hue::render`,
`hue::anim`, `hue::physics`, `hue::audio`, `hue::asset`, `hue::combat` and
`hue::ecs`. `game/` is the reference game: `hue_gameplay` holds the encounter,
actors, scenes, HUD, camera and audio events, and `game/src/main.cpp` wires it
all together. The quickest start is to copy `game/` to a folder of your own
(say `my_game/`), rename the two targets in its `CMakeLists.txt`
(`hue_gameplay` and `hue_game`), add `add_subdirectory(my_game)` to the root
`CMakeLists.txt`, and change it freely from there. The parts of that
`CMakeLists.txt` that matter for any game are:

```cmake
target_link_libraries(my_game PRIVATE my_gameplay hue::ecs)  # engine libs come through my_gameplay
hue_target_runtime_deps(my_game)          # runtime libraries beside the executable
hue_target_shaders(my_game ${CMAKE_SOURCE_DIR}/assets/shaders/mesh.vert ...)  # compile GLSL to SPIR-V
```

New characters are glTF 2.0 (`.glb`) files with a skin and animation clips,
plus an `.events.json` file naming when each attack is active and when a dodge
is invulnerable; see `assets/models/player.events.json`. Adding a third rig
means extending `RigId` in `game/src/scene_config.h` and the model list in
`game/src/main.cpp`.

House rules the engine code follows, worth keeping in your game: no exceptions
or RTTI (errors return `hue::Result`), allocations go through the tagged
allocators in `hue/core/memory.h` so the F2 memory page can account for them,
and every byte read from a file is treated as untrusted and validated.

## Local review package

Windows packaging requires Python 3.11+ and the existing Release build toolchain.
Choose a fresh output directory; the tool refuses to overwrite a package:

```sh
python tools/package.py --build build/windows-msvc --output build/packages/hue-review --require-clean
python tools/package.py --verify build/packages/hue-review
python tests/package_smoke.py build/packages/hue-review
```

The adjacent ZIP includes the executable, seven compiled shaders, models/events,
sound effects, scene/tuning data, MSVC Release runtime DLLs, controls and dependency notices.
The manifest records file hashes and source revision/dirty state. Extract the
whole ZIP, then run `hue.exe`; no compiler or Vulkan SDK is needed on the target,
but Windows x64, SSE4.2 and a Vulkan 1.3 graphics driver are required.

Capture optional local reports with `hue.exe --telemetry C:/writable/fights.jsonl`;
summarize with `python tools/fight_report.py C:/writable/fights.jsonl`. See
[telemetry and evidence limits](docs/week15.md). Runtime files and config are
executable-relative, independent of the launch directory.

## Status

Hue is a portfolio project, verified on one Windows machine (RTX 5090, MSVC).
CI builds and tests it on Windows (MSVC) and Linux (Clang, including ASan
and UBSan, with a software Vulkan driver), but nobody has played it on
Linux. It has not been checked by outside playtesters, on a physical
gamepad or on a clean machine, and multi-enemy fights are not balanced. Fuzzing
ran four hours per target with no finding, short of the 24–48 hours first
planned. Details are in [candidate scope, verification and
risks](docs/release-candidate.md) and the [development
timeline](docs/timeline.md).

## Licence

Hue is released under the [MIT License](LICENSE). Its dependencies and the
KayKit character art keep their own permissive licences; see
[third-party notices](docs/third-party-notices.txt).

## Layout

```
/engine
  /core        # platform, memory, math, containers, jobs, log
  /render      # vulkan backend, shadow + lit passes, materials, skinning
  /anim        # skeletal animation runtime: sampling, blending, events
  /audio       # bounded WAV parsing, fixed-voice mixer, output device
  /combat      # shared combat logic, hit volumes, enemy AI
  /ecs         # sparse-set ECS
  /physics     # jolt integration layer
  /asset       # gltf import (mesh+skin+anim), shader compile, hot reload
/game          # actor roster, encounter lifecycle, scenes, HUD, audio events, diagnostics
/tests         # doctest suites
/tools         # scripts: shader compile, fuzz harnesses (/tools/fuzz)
/docs          # public timeline, devlog drafts, threat-model.md
```

See [docs/timeline.md](docs/timeline.md) for the week-by-week development timeline and [docs/threat-model.md](docs/threat-model.md) for the living threat model.
