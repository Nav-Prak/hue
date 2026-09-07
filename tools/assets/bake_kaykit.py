#!/usr/bin/env python3
"""Bake KayKit Knight/Barbarian + combat clips into Hue player/enemy GLBs.

The Adventurers meshes have no clips; the Character Animations pack has the
same Rig_Medium bone names. This script copies eight LINEAR clips onto each
character, names them for the combat state machine, and skins a weapon to
the right-hand slot so it follows the attack poses.

Sources (CC0, Kay Lousberg — not committed):
  tools/assets/_kaykit_src/   extracted from the itch.io zips

Usage:
  python tools/assets/bake_kaykit.py
"""

from __future__ import annotations

import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(__file__).resolve().parent / "_kaykit_src"
MODELS = ROOT / "assets" / "models"

CLIPS = [
    ("Running_A", "locomotion", "Rig_Medium_MovementBasic.glb"),
    ("Melee_1H_Attack_Chop", "attack", "Rig_Medium_CombatMelee.glb"),
    ("Dodge_Forward", "dodge", "Rig_Medium_MovementAdvanced.glb"),
    ("Hit_A", "hit_react", "Rig_Medium_General.glb"),
    ("Death_B", "death", "Rig_Medium_General.glb"),
    ("Idle_B", "idle", "Rig_Medium_General.glb"),
    ("Walking_A", "walk", "Rig_Medium_MovementBasic.glb"),
    ("Melee_2H_Attack_Chop", "attack_heavy", "Rig_Medium_CombatMelee.glb"),
]


def load_glb(path: Path) -> tuple[dict, bytearray]:
    data = path.read_bytes()
    magic, version, length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67 or version != 2:
        raise ValueError(f"not a GLB: {path}")
    json_len, json_type = struct.unpack_from("<II", data, 12)
    if json_type != 0x4E4F534A:
        raise ValueError("missing JSON chunk")
    gltf = json.loads(data[20 : 20 + json_len])
    offset = 20 + json_len
    blob = bytearray()
    if offset + 8 <= length:
        bin_len, bin_type = struct.unpack_from("<II", data, offset)
        if bin_type == 0x004E4942:
            blob = bytearray(data[offset + 8 : offset + 8 + bin_len])
    return gltf, blob


def load_gltf_bin(gltf_path: Path) -> tuple[dict, bytearray]:
    gltf = json.loads(gltf_path.read_text(encoding="utf-8"))
    uri = gltf["buffers"][0]["uri"]
    blob = bytearray((gltf_path.parent / uri).read_bytes())
    return gltf, blob


def glb_bytes(gltf: dict, blob: bytes) -> bytes:
    payload = json.dumps(gltf, separators=(",", ":")).encode()
    payload += b" " * (-len(payload) % 4)
    blob = bytes(blob) + b"\x00" * (-len(blob) % 4)
    total = 12 + 8 + len(payload) + 8 + len(blob)
    return (
        struct.pack("<III", 0x46546C67, 2, total)
        + struct.pack("<II", len(payload), 0x4E4F534A)
        + payload
        + struct.pack("<II", len(blob), 0x004E4942)
        + blob
    )


def pad4(blob: bytearray) -> None:
    while len(blob) % 4:
        blob.append(0)


def copy_accessor(dest_gltf: dict, dest_blob: bytearray, src_gltf: dict, src_blob: bytes,
                  src_index: int) -> int:
    src = src_gltf["accessors"][src_index]
    view = src_gltf["bufferViews"][src["bufferView"]]
    start = view.get("byteOffset", 0) + src.get("byteOffset", 0)
    # Conservative copy of the whole view (stride-safe for KayKit exports).
    view_start = view.get("byteOffset", 0)
    payload = bytes(src_blob[view_start : view_start + view["byteLength"]])
    pad4(dest_blob)
    new_view = {
        "buffer": 0,
        "byteOffset": len(dest_blob),
        "byteLength": len(payload),
    }
    if "target" in view:
        new_view["target"] = view["target"]
    dest_blob.extend(payload)
    dest_gltf["bufferViews"].append(new_view)
    accessor = dict(src)
    accessor["bufferView"] = len(dest_gltf["bufferViews"]) - 1
    accessor.pop("byteOffset", None)
    dest_gltf["accessors"].append(accessor)
    return len(dest_gltf["accessors"]) - 1


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def mat4_identity():
    return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def mat4_trs(t, r, s):
    x, y, z, w = r
    sx, sy, sz = s
    xx, yy, zz = x * x, y * y, z * z
    xy, xz, yz = x * y, x * z, y * z
    wx, wy, wz = w * x, w * y, w * z
    m = mat4_identity()
    m[0] = (1 - 2 * (yy + zz)) * sx
    m[1] = (2 * (xy + wz)) * sx
    m[2] = (2 * (xz - wy)) * sx
    m[4] = (2 * (xy - wz)) * sy
    m[5] = (1 - 2 * (xx + zz)) * sy
    m[6] = (2 * (yz + wx)) * sy
    m[8] = (2 * (xz + wy)) * sz
    m[9] = (2 * (yz - wx)) * sz
    m[10] = (1 - 2 * (xx + yy)) * sz
    m[12], m[13], m[14] = t
    return m


def mat4_mul(a, b):
    out = [0.0] * 16
    for c in range(4):
        for r in range(4):
            out[c * 4 + r] = (
                a[0 * 4 + r] * b[c * 4 + 0]
                + a[1 * 4 + r] * b[c * 4 + 1]
                + a[2 * 4 + r] * b[c * 4 + 2]
                + a[3 * 4 + r] * b[c * 4 + 3]
            )
    return out


def mat4_transform_point(m, p):
    x, y, z = p
    return (
        m[0] * x + m[4] * y + m[8] * z + m[12],
        m[1] * x + m[5] * y + m[9] * z + m[13],
        m[2] * x + m[6] * y + m[10] * z + m[14],
    )


def mat4_transform_dir(m, p):
    x, y, z = p
    return (
        m[0] * x + m[4] * y + m[8] * z,
        m[1] * x + m[5] * y + m[9] * z,
        m[2] * x + m[6] * y + m[10] * z,
    )


def node_trs(node):
    t = tuple(node.get("translation") or (0.0, 0.0, 0.0))
    r = tuple(node.get("rotation") or (0.0, 0.0, 0.0, 1.0))
    s = tuple(node.get("scale") or (1.0, 1.0, 1.0))
    return t, r, s


def node_world(gltf, index, cache):
    if index in cache:
        return cache[index]
    node = gltf["nodes"][index]
    local = mat4_trs(*node_trs(node))
    parent = None
    for i, n in enumerate(gltf["nodes"]):
        if index in n.get("children", []):
            parent = i
            break
    world = local if parent is None else mat4_mul(node_world(gltf, parent, cache), local)
    cache[index] = world
    return world


def attach_weapon(char_gltf: dict, char_blob: bytearray, weapon_gltf: dict,
                  weapon_blob: bytes, slot_name: str) -> None:
    name_to_node = {n.get("name"): i for i, n in enumerate(char_gltf["nodes"])}
    slot = name_to_node[slot_name]
    skin = char_gltf["skins"][0]
    joint_index = skin["joints"].index(slot)
    world = node_world(char_gltf, slot, {})

    prim = weapon_gltf["meshes"][0]["primitives"][0]
    pos_acc = weapon_gltf["accessors"][prim["attributes"]["POSITION"]]
    nrm_acc = weapon_gltf["accessors"][prim["attributes"]["NORMAL"]]
    uv_acc = weapon_gltf["accessors"][prim["attributes"]["TEXCOORD_0"]]
    idx_acc = weapon_gltf["accessors"][prim["indices"]]

    def read_f32_view(acc, comps):
        view = weapon_gltf["bufferViews"][acc["bufferView"]]
        start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
        count = acc["count"]
        out = []
        for i in range(count):
            vals = struct.unpack_from("<" + "f" * comps, weapon_blob, start + i * 4 * comps)
            out.append(vals)
        return out

    positions = [mat4_transform_point(world, p) for p in read_f32_view(pos_acc, 3)]
    normals = [mat4_transform_dir(world, n) for n in read_f32_view(nrm_acc, 3)]
    uvs = read_f32_view(uv_acc, 2)

    view = weapon_gltf["bufferViews"][idx_acc["bufferView"]]
    start = view.get("byteOffset", 0) + idx_acc.get("byteOffset", 0)
    if idx_acc["componentType"] == 5123:
        indices = list(struct.unpack_from("<" + "H" * idx_acc["count"], weapon_blob, start))
    else:
        indices = list(struct.unpack_from("<" + "I" * idx_acc["count"], weapon_blob, start))

    def add_f32(kind, rows):
        flat = [c for row in rows for c in row]
        payload = struct.pack("<" + "f" * len(flat), *flat)
        pad4(char_blob)
        char_gltf["bufferViews"].append(
            {"buffer": 0, "byteOffset": len(char_blob), "byteLength": len(payload)}
        )
        char_blob.extend(payload)
        acc = {
            "bufferView": len(char_gltf["bufferViews"]) - 1,
            "componentType": 5126,
            "count": len(rows),
            "type": kind,
        }
        if kind == "VEC3":
            acc["min"] = [min(r[i] for r in rows) for i in range(3)]
            acc["max"] = [max(r[i] for r in rows) for i in range(3)]
        char_gltf["accessors"].append(acc)
        return len(char_gltf["accessors"]) - 1

    a_pos = add_f32("VEC3", positions)
    a_nrm = add_f32("VEC3", normals)
    a_uv = add_f32("VEC2", uvs)

    joints = [(joint_index, 0, 0, 0)] * len(positions)
    weights = [(1.0, 0.0, 0.0, 0.0)] * len(positions)
    payload = struct.pack("<" + "H" * (4 * len(joints)), *[c for row in joints for c in row])
    pad4(char_blob)
    char_gltf["bufferViews"].append(
        {"buffer": 0, "byteOffset": len(char_blob), "byteLength": len(payload)}
    )
    char_blob.extend(payload)
    char_gltf["accessors"].append({
        "bufferView": len(char_gltf["bufferViews"]) - 1,
        "componentType": 5123,
        "count": len(joints),
        "type": "VEC4",
    })
    a_joints = len(char_gltf["accessors"]) - 1
    a_weights = add_f32("VEC4", weights)

    payload = struct.pack("<" + "I" * len(indices), *indices)
    pad4(char_blob)
    char_gltf["bufferViews"].append(
        {"buffer": 0, "byteOffset": len(char_blob), "byteLength": len(payload), "target": 34963}
    )
    char_blob.extend(payload)
    char_gltf["accessors"].append({
        "bufferView": len(char_gltf["bufferViews"]) - 1,
        "componentType": 5125,
        "count": len(indices),
        "type": "SCALAR",
    })
    a_idx = len(char_gltf["accessors"]) - 1

    char_gltf["meshes"].append({
        "name": weapon_gltf["meshes"][0]["name"],
        "primitives": [{
            "attributes": {
                "POSITION": a_pos,
                "NORMAL": a_nrm,
                "TEXCOORD_0": a_uv,
                "JOINTS_0": a_joints,
                "WEIGHTS_0": a_weights,
            },
            "indices": a_idx,
            "material": 0,
        }],
    })
    mesh_index = len(char_gltf["meshes"]) - 1
    char_gltf["nodes"].append({
        "name": weapon_gltf["nodes"][0].get("name", "weapon"),
        "mesh": mesh_index,
        "skin": 0,
    })
    # Keep it in the scene graph next to the other skinned pieces.
    rig = next(i for i, n in enumerate(char_gltf["nodes"]) if n.get("name") == "Rig_Medium")
    char_gltf["nodes"][rig].setdefault("children", []).append(len(char_gltf["nodes"]) - 1)


def retarget_clips(char_gltf: dict, char_blob: bytearray) -> None:
    dest_nodes = {n.get("name"): i for i, n in enumerate(char_gltf["nodes"])}
    char_gltf["animations"] = []
    for src_name, dest_name, filename in CLIPS:
        src_gltf, src_blob = load_glb(SRC / filename)
        clip = next(a for a in src_gltf["animations"] if a.get("name") == src_name)
        new_clip = {"name": dest_name, "samplers": [], "channels": []}
        for channel in clip["channels"]:
            src_node = src_gltf["nodes"][channel["target"]["node"]].get("name")
            if src_node not in dest_nodes:
                continue
            sampler = clip["samplers"][channel["sampler"]]
            a_in = copy_accessor(char_gltf, char_blob, src_gltf, src_blob, sampler["input"])
            a_out = copy_accessor(char_gltf, char_blob, src_gltf, src_blob, sampler["output"])
            new_clip["samplers"].append({
                "input": a_in,
                "output": a_out,
                "interpolation": sampler.get("interpolation", "LINEAR"),
            })
            new_clip["channels"].append({
                "sampler": len(new_clip["samplers"]) - 1,
                "target": {"node": dest_nodes[src_node], "path": channel["target"]["path"]},
            })
        if not new_clip["channels"]:
            raise RuntimeError(f"no channels retargeted for {src_name}")
        char_gltf["animations"].append(new_clip)


def bake(character_glb: str, weapon_gltf: str, out_name: str) -> None:
    gltf, blob = load_glb(SRC / character_glb)
    retarget_clips(gltf, blob)
    if weapon_gltf:
        wgltf, wblob = load_gltf_bin(SRC / weapon_gltf)
        attach_weapon(gltf, blob, wgltf, wblob, "handslot.r")
    gltf["asset"]["generator"] = "hue bake_kaykit"
    gltf["buffers"] = [{"byteLength": len(blob)}]
    out = MODELS / out_name
    out.write_bytes(glb_bytes(gltf, blob))
    print(f"{out_name}: {out.stat().st_size} bytes, {len(gltf['animations'])} clips, "
          f"{len(gltf['meshes'])} meshes")


def main() -> None:
    missing = [name for _, _, name in CLIPS if not (SRC / name).exists()]
    for required in ("Knight.glb", "Barbarian.glb"):
        if not (SRC / required).exists():
            missing.append(required)
    if missing:
        raise SystemExit(
            "KayKit sources missing in tools/assets/_kaykit_src: "
            + ", ".join(missing)
        )
    bake("Knight.glb", "sword_1handed.gltf", "player.glb")
    bake("Barbarian.glb", "axe_2handed.gltf", "enemy.glb")


if __name__ == "__main__":
    main()
