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

No third attack, boss phase, parry, additional enemy, shadow map, general editor,
render graph, bindless system, archetype ECS, ragdoll or Jev integration is required
for this candidate. These were optional or outside the minimum slice; bypassing
acceptance gates does not make them implemented. Pose interpolation and broad
resolution/DPI polish are not implemented or certified.

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
The close follow camera can obscure the enemy at the initial alignment. Short
fuzz runs and passing sanitizers are not a security proof; local shader-validator
fuzz execution remains unverified despite its registered CI harness.

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
