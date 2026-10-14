HUE - Windows v1 candidate (external acceptance waived by owner)

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
F2 / Back: cycle game, combat diagnostics, CPU allocation diagnostics
F5: validate scene edits        F6: validate tuning edits
R applies valid scene/tuning edits. Bad edits keep the working data.
F1: debug fly camera            Escape: quit

Optional local telemetry: hue.exe --telemetry C:/writable/fights.jsonl
Create the parent folder first. No telemetry is sent over the network.
Optional rendering check: hue.exe --require-renderer --frames 120
Lifecycle regression: hue.exe --encounter-smoke --require-renderer
The lifecycle smoke injects outcomes; it is not a balance or human playtest.

KNOWN LIMITS / ACCEPTANCE
On October 14, 2026 the owner waived external acceptance gates to finish the
existing two-attack scope. Waived does not mean tested or passed. Read
RELEASE-NOTES.md for the frozen scope, evidence and remaining risks.
One arena, one enemy, two attacks. No parry, third attack, boss phase, shadows,
pose interpolation or general editor. Physical gamepad and clean-machine checks,
two external playtesters, Linux CI, sustained fuzzing and saved Tracy profiling
have not been completed and no longer block this local candidate. Local tests,
sanitizers and runtime input validation remain enabled. Frame pacing on the
author's PC is not a hardware guarantee.
If Vulkan initialization fails, the normal executable can keep a blank window
for diagnostics; use --require-renderer to fail explicitly. See terminal output.

Debug commands listen only on loopback and require --debug-channel. Tracy is
compiled on-demand and listens on localhost; neither is multiplayer support.
Do not use developer interfaces or edited shaders as a security sandbox.

manifest.json contains SHA-256 hashes, build revision and dirty-tree status.
These detect accidental changes; they are not signatures or proof of provenance.
licenses/ and third-party-notices.txt contain shipped dependency notices.
The project has no declared project-wide license; this private review package
does not introduce one. No public release or redistribution approval is implied.
