# Week 14 safe iteration and shader contracts

Engineering checkpoint, October 2, 2026. External acceptance remains pending.

## Implemented

`assets/config/tuning.json` is required beside the executable. F6 validates edits;
R applies staged tuning and scene changes on restart. The opt-in debug channel
offers `tuning.reload`, `scene.reload`, `fight.status`, `fight.start`, `fight.reset`
and `shader.reload`. Invalid files retain both active and previously staged data.

The exact schema has version 1, an integer revision, and these ordered arrays:

| Field | Components |
|---|---|
| movement | walk speed, run speed, idle threshold (metres/second) |
| dodge | speed, duration (seconds) |
| vitals | health, poise, poise regeneration delay and rate |
| light_strike / heavy_strike | health damage, poise damage, hitstop seconds, shake |
| ai_ranges | aggro, deaggro, attack (metres) |
| ai_speeds | walk, run, strafe (metres/second) |
| ai_times | notice, recover, lost-sight memory (seconds) |
| ai_attacks | light weight, light cooldown, heavy cooldown (seconds) |
| cancel_scale | close scale, open scale (relative to original sidecar times) |
| lock_ranges | acquire, release (metres) |

`input_buffer` is a scalar duration in seconds. Numeric bounds and relationships
are specified by `valid_tuning`; scaled events must also remain ordered and inside
each real clip. The active animator is reset before event storage changes. Twenty
resets prove scaling does not compound; tests demonstrate a 0.18-second close
becoming 0.09 seconds only after restart.

Shader reload now runs pinned SPIRV-Tools semantic validation and a deliberately
restricted fixed-interface check before Vulkan module creation. Runtime dependency
reuse was explicitly approved. Changing layouts requires an engine rebuild;
rig/mesh/animation asset hot replacement is not implemented. Candidate pipelines
are transactional, and resource counters make failed/successful reloads observable.

## Verification and limits

- Windows Release: 149/149 CTest tests, no skips, 96.96 seconds.
- Windows ASan: 149/149, no skips, 141.74 seconds, including the instrumented
  SPIRV-Tools validator and real GPU reload test.
- Release GPU test: 20 accepted plus 20 rejected shader reloads, stable four
  pipelines / three layouts / zero retained modules and stable tagged live bytes.
  Maximum accepted reload response: 0.065 seconds. This is local RTX 5090 evidence,
  not a promise for other hardware or a driver-memory leak proof.
- Tuning ASan/libFuzzer smoke: 9,769,001 executions in 61 seconds, exit 0, no
  reported finding. Semantic/reflection fuzz harness and seven authored shader
  seeds are registered in Linux CI, but that job has not run here. Local linkage
  of the installed Clang libFuzzer with MSVC ASan SPIRV-Tools failed due to CRT/
  sanitizer object incompatibility; no local shader fuzz success is claimed.
- Unit tests validate all seven compiled shader interfaces and reject malformed,
  wrong-binding, unknown-name and oversized inputs; unaligned valid input is copied
  safely. Integration verifies invalid scene/tuning edits preserve pending data.

Reproduce with the Windows Release/ASan build and CTest presets. Run
`tests/reload_smoke.py <absolute-path-to-hue.exe>` for the isolated-copy live test.
Linux CI, sustained fuzzing, external playtests, physical gamepad, clean-machine
acceptance and a saved Tracy capture remain open. Two attacks remain the scope.
