HUE v1.0.0 - Windows
https://github.com/Nav-Prak/hue

Extract the entire archive before running hue.exe. Keep assets/ and shaders/
beside it. Windows x64, SSE4.2 CPU and a working Vulkan 1.3 graphics driver are
required. MSVC Release runtime DLLs are bundled; a Vulkan SDK is not required.
The package has only been tested on the development machine, not a clean PC.

CONTROLS
Enter / gamepad Start: begin; return to ready after victory or defeat
R: reset to ready at any time
WASD / left stick: move; Shift to run (stick magnitude controls pad speed)
Hold right mouse / right stick: orbit camera
Left mouse / X: light attack     E / Y: heavy attack
Space / B: dodge                Tab / right-stick click: lock-on
Q / right bumper: next lock-on target
F2 / Back: cycle game, combat diagnostics, CPU allocation diagnostics
F5: validate scene edits        F6: validate tuning edits
F7: stage the next shipped scene (arena, gauntlet)   M: mute
R applies valid scene/tuning edits. Bad edits keep the working data.
Start in a named scene with: hue.exe --scene gauntlet
F1: debug fly camera            Escape: quit

Optional local telemetry: hue.exe --telemetry C:/writable/fights.jsonl
Create the parent folder first. No telemetry is sent over the network.
Silent run: hue.exe --no-audio   Device check: hue.exe --audio-smoke
Optional rendering check: hue.exe --require-renderer --frames 120
Lifecycle regression: hue.exe --encounter-smoke --require-renderer
The lifecycle smoke injects outcomes; it is not a balance or human playtest.

KNOWN LIMITS / ACCEPTANCE
On October 14, 2026 the owner waived external acceptance gates to finish the
existing two-attack scope. Waived does not mean tested or passed. Read
RELEASE-NOTES.md for the frozen scope, evidence and remaining risks.
One arena with data-driven scenes (one to seven enemies, box props, lights),
two attacks, one shadow-casting sun and procedural sound effects. No parry,
third attack, boss phase, music, pose interpolation or general editor. The
November additions have automated tests only; nobody has played or listened
to them. Physical gamepad and clean-machine checks,
two external playtesters and saved Tracy profiling
have not been completed and no longer block this local candidate. Local tests,
sanitizers and runtime input validation remain enabled. CI builds and tests
Windows and Linux, and each parser was fuzzed for four hours with no finding.
Frame pacing on the
author's PC is not a hardware guarantee.
If Vulkan initialization fails, the normal executable can keep a blank window
for diagnostics; use --require-renderer to fail explicitly. See terminal output.

Debug commands listen only on loopback and require --debug-channel. Tracy is
compiled on-demand and listens on localhost; neither is multiplayer support.
Do not use developer interfaces or edited shaders as a security sandbox.

manifest.json contains SHA-256 hashes, build revision and dirty-tree status.
These detect accidental changes; they are not signatures or proof of provenance.
Hue is released under the MIT License (LICENSE.txt). licenses/ and
third-party-notices.txt contain the shipped dependency and asset notices.
