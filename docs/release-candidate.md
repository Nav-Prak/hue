# Hue Windows v1 candidate

On October 14, 2026, the owner requested bypassing external acceptance gates and
committing the work to finish the engine quickly. This supersedes the earlier
instruction to stop at those gates. The existing two-attack vertical-slice scope
is frozen for a local Windows candidate. A waiver is a scope/risk decision, not
evidence that an unperformed test passed.

## Implemented scope

Hue provides a C++20/Vulkan 1.3 runtime with custom tagged memory/containers,
math/jobs, skeletal animation, sparse-set ECS and Jolt physics. The playable
slice has one arena, a controllable Knight and an AI Barbarian, idle/walk/run
animation, light/heavy attacks, dodge, input buffering/cancel windows, health,
poise, stagger/death, hitstop, camera shake and single-enemy lock-on.

The encounter supports ready, fight, victory/defeat and restart without relaunch.
Movement, animation, combat and AI advance at 60 Hz. Actor-owned poses remain
valid on zero-tick/hitstop frames. Diagnostics show combat/AI state, bound volumes
and tagged CPU memory. Scene/tuning edits validate before staging and apply on
reset; shader reload checks semantics and the fixed renderer interface before
transactional pipeline replacement. Per-fight JSON distinguishes interactive,
automated and injected sources. Runtime files are executable-relative.

The package contains the executable, Release CRT, shaders, models/events,
configuration, controls, release notes and dependency notices. Its manifest
records source revision/dirty state, hashes and the explicit external-gate waiver.
Use `--require-clean` when packaging the final candidate; dirty sources are
rejected rather than silently presented as the committed build.

## Scope cuts and decisions

No third attack, boss phase, parry, general editor, render graph, bindless
system, archetype ECS, ragdoll or Jev integration is required for this
candidate. These were optional or outside the minimum slice; bypassing
acceptance gates does not make them implemented. Pose interpolation and broad
resolution/DPI polish are not implemented or certified. Additional enemies, a
shadow map and audio were cut from the October 14 candidate and added afterwards
at the owner's request; see "November engine extension" below.

External playtester acceptance, clean-target-machine acceptance, physical gamepad
acceptance and Linux CI as a Windows completion gate are **owner-waived**. Linux
remains an unverified target, not a platform validated by these Windows results.
The 24–48-hour fuzz campaign, saved Tracy CPU/GPU capture and interactive
before/after balance study are **deferred evidence**, not completed work. Their
absence no longer blocks this local fast-track candidate. No test, sanitizer,
parser validation or CI job is disabled by this decision.

Weeks 17–20 are therefore not claimed to have met their original DoDs. Their
external evidence/publication requirements are waived, deferred, or still require
an explicit publishing action. The base implementation is complete for the scope
above; this is not a claim of a general-purpose finished engine or certified
production readiness.

## Verification and risks

The October 14 fast-track verification rebuilt both configurations and passed
157/157 Windows Release tests (99.65 s) and 157/157 Windows ASan tests (145.02 s).
This includes the packaging policy regressions, rendered encounter lifecycle and
real GPU reload tests. The Release reload run accepted twenty valid edits and
rejected twenty invalid edits with stable tracked resources (maximum response
0.082 s). Local logs: `build/fasttrack-release-ctest.log` and
`build/fasttrack-asan-ctest.log`. These runs do not replace the waived checks.

The October 2 baseline passed 157/157 Windows Release tests and 157/157 Windows
ASan tests. Packaged Vulkan lifecycle checks covered twenty injected outcomes and
restarts; reload checks covered twenty good and twenty rejected shader edits with
stable tracked resources. UI checks covered start, natural defeat, reset, lock-on,
diagnostics and maximize/restore. See the Week 12–16 source-tree notes for detail.

The original isolated pacing run was uneven (p50 1.282 ms / p95 33.363 ms).
A foregrounded repeat measured 16.681 / 16.895 ms on RTX 5090 at 1280x720;
these are presentation-paced wall intervals, not isolated CPU/GPU timings. Other
machines, prolonged play, gamepad handling and combat fairness are not certified.
The close follow camera that could obscure the enemy at the initial alignment
was replaced by the November 11 shoulder view below; its framing is covered by
projection tests, not by a human readability check. Short
fuzz runs and passing sanitizers are not a security proof; local shader-validator
fuzz execution remains unverified despite its registered CI harness.

## November 11 completion pass

The work left uncommitted after the October 14 freeze was verified and landed;
no scope was added.

- **Camera.** The follow camera is now an angled over-the-shoulder view with a
  longer boom (4.8 m), and lock-on keeps that angle so the enemy stays beside
  the player rather than behind them. The shoulder offset and boom both probe
  static geometry, and a new obstruction shortens the boom on the same frame.
  Tests project both fighters at 4:3, 16:9 and 21:9 and exercise both probes.
- **HUD.** Bitmap text uses square pixels at every aspect ratio and shrinks to
  fit rather than running off the right edge; the ready/result banner moved off
  the fighters. Pixel-alignment and on-screen checks cover seven framebuffer
  sizes and all three diagnostic pages.
- **Fuzz campaign runner.** `tools/fuzz_campaign.py` and
  [its procedure](fuzz-campaign.md) prepare the deferred sustained run; CI
  exercises the runner briefly. The 24–48-hour campaign itself has still
  not been run.
- **Smoke timeouts.** The rendered encounter and package smokes are paced by
  presentation. On this pass the desktop presented at about 11 frames per
  second (the October package build measured the same in the same session), so
  the 2,400-frame run needed about 180 s and exceeded its 160 s limit. The limits
  are now 420 s; the gameplay assertions are unchanged.

Verification on the development machine (RTX 5090, MSVC): Windows Release
161/161 CTest tests (138.49 s); Windows ASan 161/161 (292.35 s). Logs: `build/final-release-ctest.log`
and `build/final-asan-ctest.log`. Nobody has looked at the new camera or HUD in
a running window on this pass, and the owner-waived and deferred items above are
unchanged.

## November engine extension

After the completion pass the owner asked for four engine features beyond the
frozen slice. They are implemented and covered by automated tests on the
development machine; none has been playtested, and nobody has listened to the
audio.

- **Multi-actor encounters.** One player and one to seven enemies. Each enemy
  runs its own AI; a swing can hit each victim once; every enemy must fall to
  win; Q / right bumper moves the lock-on between living enemies in range.
  Actors that share a rig share its mesh, clips and GPU upload. Enemy capsules
  do not collide with each other, so crowding enemies steer apart instead.
- **Scene loading.** Version 2 scene files describe actors, static box props
  with collision, point lights and the sun. Scenes are chosen by whitelisted
  name (`--scene`, F7, `scene.load`) and applied at reset, reusing characters
  and controllers. Two scenes ship: `arena` and `gauntlet` (three enemies, four
  props). Version 1 scene files are rejected, not migrated. The arena mesh
  itself is still built in code; props add to it rather than replace it.
- **Shadow map.** One 2048 x 2048 map for the sun, 3x3 PCF, covering a 16 m
  radius around the arena centre; ground beyond that is simply lit. Point
  lights cast no shadows. A window capture of both scenes showed characters,
  props and the floating cube casting onto the floor without acne; that is a
  single look on one GPU, not a quality study.
- **Audio.** Twelve project-authored procedural sound effects driven by
  encounter events, with distance attenuation and panning. Hue parses the WAV
  files itself; miniaudio only opens the output device. No music. `--no-audio`
  and M (mute) are available, and a machine without an output device runs
  silently.

Verification: Windows Release 182/182 CTest tests (110.39 s); Windows ASan 181/181
(160.77 s). Logs:
`build/final-release-ctest.log` and `build/final-asan-ctest.log`. The Release
run includes the silent audio-device smoke on this machine's real output; that
test is not registered under ASan. Two later tests (30,000 deterministic byte
mutations each of WAV and scene input) also pass in both configurations; they
are not a substitute for the libFuzzer targets.

Further evidence gathered on the same machine (RTX 5090, 1280x720, MSVC
Release), all from commit `b928e6d`:

- **Package.** `build/packages/hue-v1-b928e6d` was built with `--require-clean`,
  verified, and passed the package smoke (twenty injected outcomes and restarts
  from a copy outside the checkout, all twelve sounds loading) and the reload
  smoke (twenty accepted and twenty rejected shader edits, scene switching).
  Still not a clean-machine check.
- **Frame pacing with the shadow pass.** The Week 12 two-page scenario measured
  p50 16.687 ms / p95 16.861 ms from the package (Week 12: 16.683 / 16.816;
  Week 16: 16.681 / 16.895). The four-actor `gauntlet` scene measured
  16.683 / 16.885. These are presentation-paced wall intervals: they show the
  frame still fits the display interval, not what the shadow pass costs.
- **Saved Tracy capture.** A 25-second capture of the rendered `gauntlet`
  encounter smoke (1,489 frames) is at `build/evidence/encounter-b928e6d.tracy`
  with a zone export beside it. Mean CPU time per call: `Encounter::tick`
  21.9 us for four actors, `physics::update` 11.2 us, `combat_hud` 51.7 us,
  `Animator::update` 2.9 us. `Renderer::draw_frame` averages 16.57 ms because it
  waits for presentation; the capture has CPU zones only, no GPU timings, so it
  does not isolate render cost. The scenario injects outcomes and is not
  representative play.
- **Clang.** GitHub Actions did not run for these commits: every job reports
  "not started because your account is locked due to a billing issue". As a
  partial substitute the new and changed sources were syntax-checked with the
  Clang bundled with Visual Studio using the Linux build's warning flags
  (conversion, sign-conversion and shadow as errors) with no diagnostics in
  project code. Later the same day the engine libraries and all six fuzz
  targets were built from a clean clone in WSL2 Ubuntu 24.04 with Clang 18,
  ASan and UBSan, without warnings in project code, and a one-minute campaign
  across the six targets passed with no findings. The Linux game executable
  and the Linux test suite have still not been built or run for this work.

Not done: gamepad checks of target switching, any balance work for multi-enemy
fights (the shipped tuning was made for one enemy). The sustained fuzz
campaign, now six targets, has not completed. The first attempt stopped after
11 of 24 hours on a runner timeout and, on inspection, had logged undefined
behaviour in cgltf on misaligned glTF accessors; the loader now rejects those
files. A second attempt was cut short after one hour when the WSL virtual
machine shut down. A third stopped at 3.4 hours on a second finding, a
zero-length `memcpy` with a null pointer inside stb_image, now also fixed. A
fourth stopped at 5.6 hours on a false timeout caused by the WSL clock
stepping backwards, with no parser finding; the runner now verifies and
records that case. A fifth ran twelve hours, the longest so far, before a
two-second clock step the runner did not yet recognise stopped it, again
with no parser finding. A sixth was cut short by a restart of the PC. The
runner was then rebuilt so that each target completes on its own, targets
run in parallel and any interruption resumes; a campaign under it was
started on November 17.
[Its run log](fuzz-campaign.md#run-log) says what has been observed. The owner-waived and deferred items above are otherwise
unchanged.

## Reproduce and review

```sh
cmake --build --preset windows-release
ctest --preset windows-release
cmake --build --preset windows-asan
ctest --preset windows-asan
python tools/package.py --build build/windows-msvc --output build/packages/hue-v1 --require-clean
python tools/package.py --verify build/packages/hue-v1
python tests/package_smoke.py build/packages/hue-v1
python tests/reload_smoke.py build/packages/hue-v1/hue.exe
```

Choose a fresh output directory. Python 3.11+ is needed for packaging, not to run
the game. Extract the full ZIP and run `hue.exe`; Windows x64, SSE4.2 and a working
Vulkan 1.3 driver are required. Keep assets and shaders beside the executable.
`--require-renderer` makes missing Vulkan an explicit startup failure rather than
the diagnostic fallback. Start with Enter, attack with LMB/E, dodge with Space,
lock with Tab, move with WASD, orbit with RMB and restart with R.

The owner separately authorized pushing the completed source commits on
October 14. No tag, public release, binary upload, application or marketing
publication is authorized by the acceptance waiver or source push. A
project-wide license has not been selected; dependency notices do not supply
one for Hue. No new license or redistribution permission is granted here.
