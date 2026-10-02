# Week 16 package and readability verification

Engineering checkpoint, October 2, 2026. A Windows review package can now be built
and checked locally. This is not full Week 16 acceptance: a complete fight and
restart on a clean target machine still need external evidence.

## Package contents and reproduction

`tools/package.py` builds Release, installs only the `HueRuntime` component into
a new directory, inventories the files, and creates an adjacent ZIP. It refuses
existing destinations, sanitized builds, a different source checkout, or a
single-configuration non-Release build. Python 3.11+ is required for the tool.

```sh
cmake --preset windows-msvc
python tools/package.py --build build/windows-msvc --output build/packages/hue-review
python tools/package.py --verify build/packages/hue-review
python tests/package_smoke.py build/packages/hue-review
python tests/reload_smoke.py build/packages/hue-review/hue.exe
```

Use `--cmake <absolute-path>` if CMake is not on PATH. Extract the entire ZIP on
the target and run `hue.exe`. No source-tree paths or developer SDK are needed at
runtime. Windows x64, SSE4.2 and a Vulkan 1.3 driver remain prerequisites; only the
development Windows/RTX 5090 machine has been checked. Linux packaging has not
been verified and does not bundle Linux system libraries.

The package includes the executable, seven SPIR-V shaders, two character GLBs,
animation events, scene/tuning configuration, eight installed MSVC Release runtime
DLLs, controls and dependency/asset notices. Required assets are an explicit list;
private files, sources, PDBs, development libraries and sanitizer runtimes are not
included. No extra renderer effects or third attack were added.

The manifest records SHA-256 hashes, byte sizes, source commit and dirty status.
Verification rejects missing/changed/extra files, missing notices, unexpected
shader sets, excessive file counts/sizes and symlinks. This is an accidental-change
check, not signing, a provenance guarantee or a shader sandbox. Build/package with
a clean, quiescent checkout for an unambiguous review artifact.

Hue still has no declared project-wide license. Existing dependency and asset
licenses are preserved; this checkpoint does not choose a license or authorize
publication. No release, upload, tag or push is performed by the tools.

## Live UI checks

Computer-use verification exercised the real packaged window: ready, start,
natural defeat without injected damage, Enter/R reset, lock-on feedback and
release on death, both diagnostics pages, maximize and restore. It found uneven
small bitmap glyph strokes. Glyph sizes/origins now align to framebuffer pixels;
the memory page was visibly clearer on recheck. F6 tuning is also in the controls.
Unit tests check pixel alignment at three framebuffer sizes and HUD capacity.

Local screenshots are `build/week16-ready.png`, `build/week16-defeat.png` and
`build/week16-memory.png`. These are developer evidence, not human usability
acceptance. Physical gamepad, minimize/restore, a broad resolution/DPI matrix and
extended manual combat remain open. The close follow camera can obscure part of
the enemy at the initial alignment; include camera readability in playtesting.

## Verification scope

The package smoke copies the runtime to a temporary directory outside the
checkout, launches from a different working directory with developer directories
removed from Windows PATH, requires a real renderer, and checks twenty injected
outcomes/restarts (ten each). It rechecks the manifest after shutdown. The isolated
reload test also passed twenty accepted and twenty rejected shader reloads with
stable live handles/tagged bytes, and scene/tuning rejection preserves working
data. Neither test substitutes for a clean machine.

Package-tool tests cover changed/missing/extra files, invalid manifests, missing
CRT/notices, sanitizer files and refusing to overwrite an output. Full Windows
Release: 157/157 CTest tests, no skips, 99.83 seconds. Full Windows ASan: 157/157,
no skips, 147.11 seconds. The final manifest-schema adjustment also passed all
three package-tool tests. These are not Linux CI results.

## Frame pacing comparison

Use `--encounter-smoke --baseline-hud --require-renderer` to repeat the Week 12
2,400-frame scenario alternating normal/combat HUD rather than the newer three-page
cycle. Same machine, 1280x720, MSVC Release, no telemetry file writes. Frame samples
are wall intervals including presentation pacing, not separate CPU/GPU timings.

Week 12 recorded p50 16.683 ms / p95 16.816 ms. The first isolated package run
recorded p50 1.282 ms / p95 33.363 ms, with the same 5,152-byte pose scratch peak.
The repeat without concurrent builds/tests, explicitly foregrounded after startup,
recorded p50 16.681 ms / p95 16.895 ms with the same scratch peak. Its p95 is 0.079 ms
above the Week 12 sample; this is not a controlled optimization experiment. Both
runs are retained (`build/week16-package-smoke.log` and
`build/week16-package-foreground.log`). The first uneven result is not an
optimization win; do not infer its cause from two percentiles or hide it behind
the average. A saved Tracy capture and broader pacing checks remain pending.

## Remaining acceptance gates

1. Run the extracted package on a clean target PC, retain machine/driver details,
   observe both outcomes and restart, and record missing prerequisites or failures.
2. Two external testers, including someone new, should play without spoken
   coaching. Record control discovery, dodge/punish attempts, camera visibility,
   outcome understanding, restart, and keyboard/gamepad feedback; fix and retest
   observed blockers. This is Week 17, not certified by automation.
3. Collect ten interactive fight records and a verified before/after tuning
   improvement. Automated/injected samples are not a substitute.
4. Run Linux CI and the sustained 24–48-hour parser campaign; retain actual target
   time/coverage and findings. Local smoke runs do not close Week 18.
5. Save Tracy evidence, investigate pacing, finish supported-machine and clean
   checkout release checks, then select a candidate. Publication remains an
   explicit later action after owner review; Weeks 19–20 are not accepted.
