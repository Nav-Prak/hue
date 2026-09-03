# Character models

`player.glb` and `enemy.glb` are **generated placeholder characters**, not
downloaded assets. They are produced deterministically by
`tools/assets/generate_characters.py` (pure Python stdlib — regenerate any
time with `python tools/assets/generate_characters.py`).

## Why generated instead of Mixamo

The original plan named Mixamo characters for the Week 6 import milestone.
That was deliberately substituted:

- **Licensing** — this is a public repo; Mixamo's terms allow use in
  projects but not redistribution of the raw assets. Generated rigs are
  ours outright and can live in git.
- **Determinism** — the exact bytes are reproducible from a small script,
  which also makes them honest fuzz-corpus seeds and unit-test fixtures
  (`tests/assets/test_gltf.cpp` asserts against their structure).
- **The importer doesn't care** — `load_gltf_skinned` accepts any rigged
  GLB within its caps (≤256 joints). A Mixamo-class rig drops in later by
  replacing these files; nothing in the engine is shaped around the
  placeholder.

## What they contain

Everything the Week 6 spec requires the importer to handle:

- 19-joint humanoid skeleton (hips → spine → chest → neck → head; shoulder
  → upper arm → forearm → hand per side; thigh → shin → foot per side)
  with inverse bind matrices.
- Smooth skinning: limb-tube vertices blend between the two nearest
  joints (real multi-influence weights, u16 joints / f32 weights).
- Embedded 64x64 PNG base color texture + metallic-roughness material.
- Seven LINEAR clips: `locomotion` (run), `attack`, `dodge`, `hit_react`,
  `death`, `idle`, and `walk` — the set the combat state machine consumes
  from Week 9. `walk` shares the 0.8s period of `locomotion` so the 1D
  speed blend can drive both gaits from a single phase.

The Week 7 runtime samples these clips into frame-arena poses, crossfades
between locomotion and one-shots, and uploads joint palettes for GPU skinning.
The adjacent `*.events.json` files provide validated attack, cancel, hitbox,
invulnerability, and footstep timing without modifying the GLBs.

Since Week 8 the live demo drives the walk↔run 1D blend from the capsule
controller's solved ground speed (idle below 0.2 m/s, walk at 1.6, run at
4.0), with the third-person follow camera orbiting the player.
