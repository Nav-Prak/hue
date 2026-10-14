# Hue — Public Development Timeline

**July 6 → November 19, 2026.** Twenty numbered weeks; the final week is four days. From-scratch C++20/Vulkan third-person combat runtime, built in public. November 19 is Hue's project target, independent of external release schedules.

## Fast track scope decision — October 14, 2026

The owner now authorizes bypassing external acceptance gates. The two-attack
Windows v1 scope is frozen; external playtest, clean-machine, physical-gamepad and
Linux CI acceptance no longer block the local candidate. Extended fuzz/profiling
and human balance evidence remain unperformed/deferred, not passed. Local tests,
sanitizers, runtime validation and CI jobs remain enabled. Optional content and
engine expansion stay out of scope. See [the candidate scope and risks](release-candidate.md).

This supersedes the stop-at-unmet-gates decision. Original weekly targets below
remain historical requirements, not blanket completion claims. Publication and
pushing still require the owner's review and instruction.

## Engineering sequencing update — October 2, 2026

The owner authorized proceeding through independent engineering milestones,
ignoring calendar dates. Local commits record tested implementation; external
playtest, clean-machine, supported-platform and sustained-campaign gates remain
separate and must not be inferred from those commits. The earlier requirement to
wait for full external acceptance before scene/tools implementation is superseded.
Engineering evidence: [Week 13 diagnostics and scenes](week13.md),
[Week 14 tuning and shader validation](week14.md), [Week 15 telemetry](week15.md),
and [Week 16 packaging](week16.md). These are implementation checkpoints, not
blanket DoD acceptance. Week 17 playtests, Week 18 sustained campaign, Week 19
release verification and Week 20 publication remain unaccepted. Nothing is
published automatically.

## Implementation update — September 22, 2026

Week 12 code is integrated: encounter lifecycle/reset, owned last-pose storage,
bitmap prompts, joint-volume/state diagnostics, fight counters, reset/timing
regressions, real-game encounter smoke, and an event-JSON fuzzer. See
[verification and outstanding acceptance gates](week12.md). External playtests,
physical gamepad acceptance, Linux CI results and a saved Tracy capture remain
separate from local implementation/test completion. Week 13+ scope is unchanged.

## Historical rebaseline — September 20, 2026

Reviewed against source at `aa1b8f7` (Week 11). This records implementation and test registration, **not a fresh runtime, CI, or playtest acceptance result**. Earlier weekly targets are not automatically completed DoDs.

| Current state | Evidence | Planning consequence |
|---|---|---|
| Sampling, crossfades, GPU palettes and Tracy zones; live idle crossfades plus walk/run 1D speed blend | [Animation runtime](../engine/anim/src/animation.cpp), [character integration](../game/src/combat_character.cpp) | Week 7's single-clip fallback is historical. Today's locomotion blend is integrated, but is not a general blend-tree graph. |
| AI, physics, combat and animation advance on the fixed 60 Hz tick; controller motion feeds ECS transforms | [Game loop](../game/src/main.cpp) | World movement exists. Audit pose lifetime and frame-rate independence; fixed ticking alone does not prove render interpolation. |
| KayKit player/enemy with eight clips each; light/heavy, dodge, poise, death, hitstop/shake and distance/LOS AI | [Asset notes/license](../assets/models/README.md), [combat](../engine/combat/include/hue/combat/combat.h), [AI](../engine/combat/include/hue/combat/ai.h) | Tune current content and the shared combat path before adding new attacks. |
| Two actor slots, one enemy lock-on, health-bar quads | [Game loop](../game/src/main.cpp) | Start/results/restart and readable prompts remain. General target switching requires multi-enemy scope. |
| Component tests and platform/render/debug-channel smoke tests; no encounter lifecycle test registered | [Test targets](../tests/CMakeLists.txt) | Add integration/reset coverage; rendering one frame is not proof of a playable fight. |
| Logs/Tracy exist; live combat debug overlays remain pending | [Game](../game/src/main.cpp), [profiling](../engine/anim/src/animation.cpp) | Carry forward the unmet Week 10 hitbox-visualization gate. |
| Only glTF fuzz target is registered; scene/tuning parsers are not yet implemented | [Fuzz targets](../tools/fuzz/CMakeLists.txt), [CI](../.github/workflows/ci.yml), [threat model](threat-model.md) | Add overdue event-JSON fuzzing in Week 12; scene/tuning harnesses accompany their loaders. |

The critical path is **repeatable encounter → diagnostic/tuning tools → playtested package → release evidence**. Keep one arena, one player, one enemy, and two existing attacks as the minimum release. A third attack/phase change is gated content, not a reason to miss the date.

## Historical targets — Weeks 0–11

These preserve milestone intent, not blanket completion claims. Week 8 landed Sep 3, Week 9 integration Sep 7, Week 10 Sep 15, and Week 11 Sep 17. The baseline above and future gates below take precedence over historical targets.

### Week 0 — Animation feasibility spike (before Jul 6)
Load one rigged character via cgltf, sample one clip, compute skinning matrices, fire `hit_begin`/`hit_end` from a sidecar JSON event track at the right timestamps. Gate: pass → full custom animation runtime; struggle → crossfade-only fallback.

## Phase 1 — Foundation (Weeks 1–3, Jul 6–Jul 26)

**Week 1 — Skeleton + platform layer.** Public repo, CMake superbuild, CI (Windows MSVC + Linux Clang) with the sanitized job from day one. GLFW window, input abstraction (keyboard/mouse/gamepad), fixed-timestep loop, leveled ring-buffered logging.

**Week 2 — Memory + containers.** Frame arena, pool allocator, tagged heap tracking, debug guards (canaries, free-poisoning, guard pages), `checked_mul`/`checked_add`, bounds-checked `Array<T>`/`HashMap`/`StringId`, allocation overlay.

**Week 3 — Math + job system.** vec/mat/quat, AABB, frustum, SSE4.2 SIMD, spring/damping helpers. Job interface + fixed thread pool + parallel-for.

## Phase 2 — Renderer + assets (Weeks 4–7, Jul 27–Aug 23)

**Week 4 — Vulkan bring-up.** Instance/device/swapchain, VMA, dynamic rendering, 2 frames in flight, triangle. GLSL → SPIR-V toolchain with a `spirv-val` gate and runtime recompile hook.

**Week 5 — Static meshes + camera + first parser.** Vertex/index buffers via staging, depth buffer, debug fly camera. cgltf import with all accessor offsets/counts validated against buffer sizes. First fuzz harness (glTF) in CI; threat model started.

**Week 6 — Textures + PBR + skinned mesh loading.** Mipmapped images, sampler cache, sRGB correctness, metallic-roughness PBR. glTF skins (joints, inverse bind matrices, skinned vertex format); fuzz corpus extended with skinned/animated samples. Import player + enemy with locomotion/attack/dodge/hit-react/death clips.

**Week 7 — Skeletal animation runtime (critical path).** Clip sampling (T/R/S tracks, quaternion slerp), frame-arena poses, GPU skinning, crossfades and sidecar events. Single-locomotion-clip fallback at this milestone: in-place playback did not mean physical movement or a live 1D blend tree. Fixed 60 Hz simulation and Tracy sampling zones are distinct requirements from measured 60 fps rendering. Live idle/walk/run integration and world movement followed in Week 8.

## Phase 3 — Combat vertical slice (Weeks 8–12, Aug 24–Sep 27)

**Week 8 — ECS + character on the ground.** Sparse-set ECS (entity IDs, dense component arrays, queries, deferred structural changes). Jolt static arena collision, capsule controller, gravity/slopes. Third-person follow camera with spring-arm collision probe.

**Week 9 — The attack.** Combat state machine (locomotion/attack/dodge/hit-react/death) gated by anim events. Light + heavy attacks, input buffering, dodge with i-frames, one-enemy lock-on (range-limited acquire, camera bias, strafe while locked). General nearest-enemy selection is not implemented by a single-enemy toggle.

**Week 10 — Hitboxes, health, hit reactions.** Hitbox/hurtbox shapes attached to joints, activated by anim events, overlap via Jolt queries, one-hit-per-activation. Health/damage, hit-react (stagger with poise threshold), death. Hitstop + camera shake.

**Week 11 — Enemy AI.** State machine: idle → aggro (distance/LOS) → approach → attack selection (weighted choice + cooldowns) → recover, with strafe/reposition. Enemy reuses the player's combat/hitbox systems.

## Week 12 — Repeatable encounter and slice hardening (Sep 21–27)

Work in this order. No scene parser, general editor, target switching or new combat verb this week.

1. **Encounter ownership and states.** A small game-side session owner, not an engine/ECS rewrite: ready → fighting → won/lost → restart. Define terminal input/damage/AI behavior and simultaneous-lethal resolution; allow death/result presentation to finish.
2. **Complete restart.** Reset HP/poise/regen, combat buffers/flags, animation time/fades/events, AI/cooldowns/seed, controller position/velocity, ECS transforms, dodge state, hit bookkeeping, lock-on, hitstop, camera springs/shake and counters. Flush/clear pending structural work. Reuse immutable assets; refresh recreated handles/draw bindings without accumulating bodies/resources.
3. **Timing and lifetime checks.** Frame-arena palettes/global poses are retained by draw/character state although an update can be skipped after arena reset. Establish a valid last-pose lifetime through submission. Test zero-tick, multi-tick, hitstop-only and restart frames, one-shot input consumption, short event windows, hit activation and death ordering. Treat interpolation as separate work, justified by measured jitter.
4. **Minimum usable UI and diagnostics.** Reuse health bars. Add readable start/control/result/restart prompts and lock-on feedback via a minimal text path, not a UI framework project. Toggle actual bound hit/hurt volumes and essential combat/AI flags. Add per-fight counters and structured log summaries; capture Tracy/memory baseline.
5. **Regression and playtest.** Add lifecycle/reset integration tests and event-JSON fuzz target/corpus/CI. Test both outcomes, terminal input spam, keyboard/gamepad, resize and lock-on release on death. Run 20 restarts with stable live actor/body counts and no monotonic memory growth after warm-up. Compare gameplay events/outcomes for identical tick-indexed input at synthetic 30/60/144 Hz frame schedules, not cross-platform bitwise physics. Playtest with 2+ other people for at least three minutes each using in-game prompts; fix/retest top blockers.

**Acceptance:** start, fight, understand either outcome and restart without relaunch or spoken coaching; lifecycle/timing tests and affected sanitized tests pass; actual rendering, controls and reset checks have recorded evidence. This is the portfolio insurance milestone. Week 13 first closes any missed gate.

## Phase 4 — Tools + security consolidation (Weeks 13–15, Sep 28–Oct 18)

### Week 13 — Diagnostics and bounded scene configuration (Sep 28–Oct 4)

- Complete combat/AI overlays: buffered action/expiry, cancel/i-frame/hit flags, HP/poise, perception/LOS/cooldowns, entity/body counts, frame time, tagged allocation and arena peaks. Debug volumes must match collision transforms. Docking/general inspection is stretch.
- After Week 12 acceptance, add minimal versioned scene JSON for the existing arena and two spawns, with approved asset IDs, byte/count/string caps, finite/ranged values and validated references. Reuse encounter initialization/reset; no editor/general entity serializer.
- Validate before committing; invalid scenes leave the current scene intact. Add parser tests, a memory-only fuzz target/corpus and CI run, and update the threat model in the same change.

**Acceptance:** diagnose a reproducible combat issue on screen; load/restart the existing fight from data; reject malformed data without partial mutation or resource growth. If time is tight, retain code-authored scenes and explicitly defer the loader; essential diagnostics take priority.

### Week 14 — Safe reload and tuning iteration (Oct 5–11)

- Extend existing shader reload (which already retains the previous pipeline on creation failure): validated reflection/interface compatibility, useful errors and safe retirement of in-flight GPU resources.
- Centralize duplicated tuning; bounded/versioned combat and AI configuration, with clip/event-window and blend-speed relationship checks. Stage updates and commit at a defined safe boundary, default next fight/restart. Do not invalidate active event-track references. Rig/mesh/clip hot replacement is out of scope.
- Fuzz CPU-side parsers/reflection validators; separately exercise real GPU reload success/failure and repeated replacement. Keep the last working data/pipeline on invalid or partial writes.

**Acceptance:** visible reload result promptly (target <1s excluding safe-boundary wait), demonstrable between-fight tuning change, no resource growth or invalid active state after failed/repeated reloads; tests, CI fuzz and threat-model entries accompany each new boundary.

### Week 15 — Telemetry and gated miniboss content (Oct 12–18)

- Export early counters to per-fight JSON with outcome, hits, dodges, staggers, duration, seed and tuning version; distinguish simulation/wall time and hitstop. Aggregate ten fights and document a measured tuning change.
- Tune readable light/heavy attacks and punish windows first. Existing clips are two chops, not the formerly promised overhead/swipe/lunge set.
- Only with accepted encounter/tools gates and suitable content, add a third gap-closer plus a once-only phase change at 50% HP through the shared combat/AI path. Verify clip/event suitability before committing; test cooldown/reset, collision-safe movement and counterplay.

**Acceptance:** ten-fight report and one verified improvement. A tuned two-attack encounter is the release fallback; do not claim the third attack/phase without integration/regression evidence. Freeze combat features at week's end.

## Phase 5 — Polish + launch (Weeks 16–20, Oct 19–Nov 19)

### Week 16 — Readability and first packaged build (Oct 19–25)

Prioritize contrast, ground contact, telegraphs and camera visibility; one directional shadow map only within the measured budget. Tune existing lighting/tonemapping before optional vignette/skybox/fog. Package runtime dependencies, shaders, models/events/config and license notices. Test away from the source tree/developer paths on a clean target machine.

**Acceptance:** complete fight/restart from the package, representative screenshots, and comparison to the Week 12 performance baseline. Packaging and clarity outrank new rendering features.

### Week 17 — External playtest and release freeze (Oct 26–Nov 1)

Two or more testers, including someone new, use the package. Observe control discovery, dodge/punish opportunities, camera occlusion, result/restart and keyboard/gamepad usability. Collect unprompted feedback; fix/retest the top three observed issues (or all if fewer). No new verbs/enemies or engine rewrites.

**Acceptance:** no crash/progression blockers; testers start, understand the outcome and restart unaided; critical feel issues are fixed/retested or explicitly release-blocking. Freeze content/features and choose a release candidate.

### Week 18 — Sustained fuzzing and security retrospective (Nov 2–8)

Start a 24–48h elapsed campaign early in the week across all shipped parser/validator harnesses. Record actual per-target time, executions, coverage and worker allocation. Triage/fix findings, preserve regression cases, and rerun affected targets. Publish observed findings (including none), untested boundaries and deferred scope, not an invented crash story or claim of proven security.

**Acceptance:** campaign artifacts/coverage, no unresolved known sanitizer findings in shipped paths, passing regressions and recorded post-fix clean reruns; updated threat model and scorecard. Fixes outrank presentation.

### Week 19 — Release verification and performance writeup (Nov 9–15)

Compare against Week 12 on the same hardware/resolution/scenario. Optimize only a measured bottleneck; publish before/after evidence if useful, or explain why extra complexity is unjustified. Run full CI/sanitizers and supported-machine renderer checks, both outcomes, repeated restarts/reload failures and input devices. Verify a clean checkout build and downloadable package with another person; record prerequisites/build time. Finish architecture/controls, GIFs, overlays, licenses and known limitations.

**Acceptance:** reproducible profile, verified package/build instructions and no known release-blocking defects. This week includes regression buffer, not new features.

### Week 20 — Launch (Nov 16–19)

Four days: final package smoke test, tested source tag/release, known limitations, 90-second demo with overlays, and development montage. No planned implementation work; publish by November 19.

## Standing acceptance and scope rules

- Distinguish **implemented**, **integrated**, and **accepted**. Each weekly note includes commit/preset, tests and skips, runtime platform/GPU/resolution, evidence and remaining gaps. Unit tests and skipped GPU smoke tests do not certify a playable game.
- Reserve the last two days of each full week for integration/regression/evidence. A slipped core gate consumes the next feature budget, not launch buffer.
- Proposed performance target: 60 fps (16.7 ms/frame) on a named reference machine/resolution. Baseline in Week 12; report p50/p95 frame time, CPU/GPU timing where available, tagged allocations and arena peaks. This is not an existing measurement or all-hardware guarantee.
- New input paths ship with validation, regression tests and fuzz/CI coverage; Week 18 is consolidation, not the first hardening pass.
- Cut multi-enemy switching/parry, elaborate docking/inspection and optional effects first; then phase-2/third-attack content if necessary. Keep code-authored scenes if a loader threatens the slice, and update the threat model accordingly. Never cut repeatable restart, essential diagnostics, validation of shipped inputs or release verification.
- Bindless, render graph, archetype ECS, work stealing, general editor, ragdolls and new gameplay modes remain outside the critical path.
