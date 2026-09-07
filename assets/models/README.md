# Character models

`player.glb` (KayKit Knight) and `enemy.glb` (KayKit Barbarian) are
**imported CC0 characters**, not the generated tubes. They are baked by
`tools/assets/bake_kaykit.py` from Kay Lousberg's Adventurers + Character
Animations packs. License: `LICENSE-KayKit.txt` (CC0, no attribution
required).

The generated 19-joint humanoid still exists as a deterministic fuzz and
unit-test fixture (`tools/fuzz/corpus/gltf/skinned_character.glb`, rebuilt
by `tools/assets/generate_characters.py`). The importer does not care
which rig it is given, as long as the file stays within the caps
(≤256 joints, LINEAR clips, embedded textures).

## Why KayKit instead of Mixamo

The original plan named Mixamo. Mixamo allows use in a finished game but
forbids redistributing the raw character/animation files, which a public
repo would do. KayKit is CC0, ships glTF, and includes a combat clip set
that maps onto the Week 9 state machine.

## What the live GLBs contain

- Rig_Medium humanoid (23 joints including hand slots) with inverse binds.
- Multi-primitive body (head, arms, legs, armor) plus a weapon skinned to
  `handslot.r` (knight: 1H sword, barbarian: 2H axe).
- Embedded PNG atlas and metallic-roughness material.
- Eight LINEAR clips, renamed for Hue:

  | Hue clip | KayKit source |
  |---|---|
  | `idle` | `Idle_B` (2.13s) |
  | `walk` | `Walking_A` (1.07s) |
  | `locomotion` | `Running_A` (0.80s) |
  | `attack` | `Melee_1H_Attack_Chop` (1.07s) |
  | `attack_heavy` | `Melee_2H_Attack_Chop` (1.63s) |
  | `dodge` | `Dodge_Forward` (0.40s) |
  | `hit_react` | `Hit_A` (0.67s) |
  | `death` | `Death_B` (2.63s) |

Walk and run do **not** share a period. The 1D speed blend wraps each
clip on its own duration.

The adjacent `*.events.json` files time cancel windows, hitboxes, i-frames,
and footsteps against those real clip lengths.

## Rebuild

Extract the two free itch.io zips into `tools/assets/_kaykit_src/`
(gitignored), then:

```
python tools/assets/bake_kaykit.py
```
