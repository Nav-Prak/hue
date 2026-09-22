# Week 12 — repeatable encounter

Implementation/verification date: September 22, 2026. This accompanies the Week 12
commit; the local build logs are ignored artifacts under `build/`, not CI results.

## Implemented and integrated

- `game/src/encounter.*` owns ready/fighting/won/lost state, queued player action,
  lock-on, AI/seed, dodge direction, hit activation bookkeeping, hitstop and
  per-fight counters. Simulation advances one fixed 60 Hz tick per call.
- Reset restores both characters, animations/events/fades, HP/poise/regen,
  controller positions/velocities/ground contacts, camera springs/shake and
  counters. Main flushes deferred ECS work and synchronizes the same two entity
  transforms/draw slots. Immutable model/GPU assets and controllers are reused.
- Both hit contacts are determined before either strike is applied. Simultaneous
  lethal hits trade; player death resolves to defeat. Terminal input/damage/AI
  and the fight timer stop, while the death pose continues to its final frame.
- Fixed-capacity actor-owned skin/global matrices survive zero-tick and hitstop
  frames. Scratch is reused per actor/tick, so catch-up does not accumulate poses.
  A sub-tick hit window is observable for collision at the sampled pose; this is
  not swept collision or continuous collision detection for weapons.
- Existing health bars now have bitmap controls/start/result/restart prompts,
  lock status and fight time. F2/Back toggles actual joint-bound capsule/sphere
  wire outlines, combat flags/buffer/poise, AI state, hit counters and memory/frame
  metrics. Wire outlines are X-ray (depth off), with gray inactive and red active
  hit volumes; curve tessellation is an approximation of the collision shapes.
- Initial camera faces the opponent from behind the player. Right-stick orbit
  has a deadzone. Sticky keyboard/mouse snapshots preserve taps that start and
  end within one OS event batch; actions queue once outside the simulation loop.
- Each result emits `fight_summary`: outcome, fixed seed, fight ticks (including
  hitstop), hitstop ticks, hits landed/taken, player-inflicted staggers and dodges.
  JSON reports/live tuning remain Week 14/15 work.
- `fuzz_animation_events` exercises bounded event JSON and clip/time validation;
  seeds and a 60-second Linux CI invocation ship with the harness.

## Verification

Local Windows/MSVC Release and AddressSanitizer suites each passed **139/139
tests**, with no failures or skips. They cover component tests, real window/Vulkan
presentation, the debug channel, and a 20-cycle encounter smoke. The final runs
took 90.76 seconds (Release) and 126.02 seconds (ASan). Rerun with:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-release
ctest --preset windows-release --output-log build/week12-release-tests.log
cmake --preset windows-msvc-asan
cmake --build --preset windows-asan
ctest --preset windows-asan --output-log build/week12-asan-tests.log
```

Encounter regression tests use real KayKit clips and Jolt, without a GPU:

- Ready/terminal input gating; both outcomes; simultaneous fatalities.
- Twenty resets, restored state/controller counts, and stable tracked live
  allocation counts/bytes after warm-up for assets, animation, physics and game.
- Scratch overwrite on a zero-tick frame and last-pose preservation in hitstop.
- Identical tick histories under synthetic 30/60/144 Hz render schedules.
- One successful hit per activation, reset/replay, and a sub-tick hit window.
- Finite/bounded HUD output and safe exhaustion of its fixed capacity.
- Restart clears timestep fractional debt without losing lifetime tick counts.

The real-game smoke draws 2,400 frames and toggles diagnostics across 20 cycles.
It starts actual attacks but injects lethal damage to guarantee alternating
win/loss cases. This validates integration/presentation/reset, not balanced wins,
physical input devices, arbitrary refresh-rate behavior, or external usability.

Visual Windows checks confirmed the ready screen, Enter-to-fight, an enemy-driven
defeat, F2 diagnostic volumes/state, and Enter-to-ready with restored health and
camera. The camera and short-tap fixes came from these checks. This is developer
verification, not the two-person external playtest gate.

Windows Clang's warning checks also compiled the changed gameplay translation
units with conversion/sign/shadow errors enabled. This does not replace Linux CI.
The available Ubuntu distribution had no CMake/Clang/Ninja/Xvfb tools, so Linux
build/runtime and the new CI job were not executed locally.

### Fuzzer smoke

The event harness plus animation implementation and tagged allocator were built
with installed Windows Clang 22, ASan and libFuzzer. A 60-second budget completed
18,375,472 executions in 61 seconds, exit 0, with no reported ASan failure.
The mutable working corpus is under `build/week12-event-corpus`; only the three
authored seeds are committed. This is a smoke campaign, not a security proof or
the Week 18 sustained run. The initial launch lacked the Clang ASan DLL; rerunning
with its runtime directory on the process PATH resolved that setup error.

Canonical Linux/CI reproduction:

```sh
cmake --preset linux-clang-sanitized -DHUE_FUZZ=ON -DHUE_TRACY_ENABLE=OFF
cmake --build --preset linux-sanitized --target fuzz_animation_events
mkdir -p build/event-corpus
cp tools/fuzz/corpus/animation_events/*.json build/event-corpus/
./build/linux-clang-sanitized/tools/fuzz/fuzz_animation_events build/event-corpus \
  -max_total_time=60 -max_len=65537 -timeout=10 -rss_limit_mb=2048
```

### Initial performance/memory baseline

Windows 11, NVIDIA GeForce RTX 5090, Vulkan, 1280×720, MSVC Release, 2,400-frame
scripted reset run with diagnostics alternating on/off. An initial run recorded
p50 **16.683 ms**, p95 **16.816 ms**, and animation scratch peak **5,152 bytes**.
These are wall-clock frame intervals including presentation pacing, not isolated
CPU/GPU timings or proof of spare GPU capacity. The first startup interval is
included; the summary retains up to the latest 4,096 samples. Other desktop
applications may affect results. Rerun on the final build before comparisons.

Observed late-run tracked live bytes: render 59,472; animation 266,368; physics
4,453,954; assets 9,947,620; ECS 1,088. These are tagged CPU allocations, not VRAM
measurements. Automated reset stability checks are separate from these snapshots.
Tracy zones remain on frame/simulation/animation/physics/HUD paths; a saved Tracy
capture and CPU/GPU breakdown have **not** been produced in this verification.

## Remaining acceptance, not missing implementation claims

- Two external testers, at least three minutes each, without spoken coaching;
  retain observations and fix/retest top blockers.
- Physical keyboard/gamepad session including camera, dodges, lock-on and result
  restart; resize/minimize and a longer manual fight. Gamepad connection alone
  is not verification of all bindings. Xbox-style labels map through GLFW.
- Linux Clang/sanitizer/fuzzer CI after review/push, and a saved Tracy capture.
- Rendering uses the last simulation pose, not interpolated poses. Assess visible
  jitter before adding interpolation. No multi-enemy target switching, scene
  JSON, expanded ImGui tools, Jev integration, or third attack was added.

The code is ready for review/playtesting; Week 12's external acceptance is not
declared complete. No push is required to run the local executable or tests.
