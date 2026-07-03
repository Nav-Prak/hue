# Hue — Public Development Timeline

**July 6, 2026 → November 19, 2026 (20 weeks).** From-scratch C++20/Vulkan third-person combat runtime, built in public.

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

**Week 7 — Skeletal animation runtime (critical path).** Clip sampling (T/R/S tracks, quaternion slerp), pose buffers on the frame arena, GPU skinning, crossfade + 1D locomotion blend. Anim events from sidecar JSON drive attack timing, cancel windows, and hitbox activation. Fallback: crossfade only + primitive hitboxes.

## Phase 3 — Combat vertical slice (Weeks 8–12, Aug 24–Sep 27)

**Week 8 — ECS + character on the ground.** Sparse-set ECS (entity IDs, dense component arrays, queries, deferred structural changes). Jolt static arena collision, capsule controller, gravity/slopes. Third-person follow camera with spring-arm collision probe.

**Week 9 — The attack.** Gameplay combat state machine (idle/locomotion/attack/dodge/hit-react/death) gated by anim events. Light + heavy attacks, input buffering, dodge with i-frames, crude lock-on (nearest-enemy acquire, camera bias, strafe while locked).

**Week 10 — Hitboxes, health, hit reactions.** Hitbox/hurtbox shapes attached to joints, activated by anim events, overlap via Jolt queries, one-hit-per-activation. Health/damage, hit-react (stagger with poise threshold), death. Hitstop + camera shake.

**Week 11 — Enemy AI.** State machine: idle → aggro (distance/LOS) → approach → attack selection (weighted choice + cooldowns) → recover, with strafe/reposition. Enemy reuses the player's combat/hitbox systems.

**Week 12 — Encounter loop + slice hardening.** Full loop: arena start → fight → win/lose → restart. Lock-on target switching, basic HUD. Scene description in validated JSON (capped counts, validated references) + scene-loader fuzz harness. Playtest with 2+ people and tune.

## Phase 4 — Tools + security consolidation (Weeks 13–15, Sep 28–Oct 18)

**Week 13 — Combat debug overlays.** ImGui docked overlays: entity inspector, combat state panel (state, buffered input, active cancel window), hitbox/hurtbox visualization, AI state + perception, frame stats + allocator overlay.

**Week 14 — Hot reload + shader path hardening.** Robust shader hot reload (error toasts, no crash on bad shader, validated reflection, fuzz harness on the reload path). Combat tuning values (damage, windows, cooldowns) in a live-reloaded JSON file.

**Week 15 — Encounter telemetry + miniboss.** Per-fight telemetry (hits landed/taken, dodges, staggers, time-to-kill) to JSON + report script. Miniboss: slow punishable overhead (A), quick short-telegraph swipe (B), phase-2 gap-closer at 50% HP (C), poise-based stagger with designed punish windows.

## Phase 5 — Polish + launch (Weeks 16–20, Oct 19–Nov 19)

**Week 16 — Visual pass.** One directional shadow map, arena lighting/mood, tonemap + vignette, skybox or fog for depth.

**Week 17 — Combat feel polish.** Second playtest round; polish animation transitions, hitstop values, lock-on camera, enemy telegraph readability.

**Week 18 — Security retrospective.** Extended 24–48h fuzz run across all harnesses with accumulated corpus and coverage reports. Crash taxonomy writeup; threat model published in full.

**Week 19 — Performance writeup + README.** Tracy frame breakdown with one real before/after optimization. README overhaul: pitch, architecture diagram, combat GIFs, overlay screenshots, verified clean-machine build.

**Week 20 — Launch (GTA 6 week).** 90-second demo video (combat with overlays toggled mid-fight) + 20-week montage devlog.
