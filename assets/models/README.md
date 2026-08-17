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
- Five LINEAR clips: `locomotion`, `attack`, `dodge`, `hit_react`,
  `death` — the clip set the combat state machine consumes from Week 9.

They render in bind pose (T-pose) until the animation runtime lands in
Week 7.
