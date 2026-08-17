#!/usr/bin/env python3
"""Generates the Week 6 rigged characters: player.glb and enemy.glb.

Mixamo-style stand-ins built in code (see assets/models/README.md for why
these are generated rather than downloaded): a 19-joint humanoid skeleton
(hips -> spine -> chest -> neck -> head, shoulder/upper-arm/forearm/hand
per side, thigh/shin/foot per side) skinned smoothly -- limb tubes whose
ring vertices blend between the two nearest joints, so vertices genuinely
carry multiple influences like production rigs do. Embedded 64x64 PNG base
color texture, metallic-roughness material, and five LINEAR animation
clips: locomotion, attack, dodge, hit_react, death. Pure stdlib; output is
deterministic so the GLBs can live in the repo and regenerate on demand.

Also emits small skinned/textured seeds for the glTF fuzz corpus.

Usage: python tools/assets/generate_characters.py
"""

from __future__ import annotations

import json
import math
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODELS_DIR = ROOT / "assets" / "models"
CORPUS_DIR = ROOT / "tools" / "fuzz" / "corpus" / "gltf"

FLOAT = 5126
UINT16 = 5123
UINT32 = 5125


# ----------------------------------------------------------------- PNG

def encode_png(width: int, height: int, rgba: bytes) -> bytes:
    """Minimal PNG encoder: 8-bit RGBA, no interlace, filter 0 rows."""
    raw = b"".join(
        b"\x00" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height)
    )

    def chunk(tag: bytes, payload: bytes) -> bytes:
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(
            ">I", zlib.crc32(body) & 0xFFFFFFFF
        )

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def character_texture(base: tuple, accent: tuple) -> bytes:
    """64x64 tunic pattern: base color with accent stripes and stitching."""
    size = 64
    pixels = bytearray()
    for y in range(size):
        for x in range(size):
            color = base
            if y % 16 in (0, 1):
                color = accent  # horizontal straps
            elif (x + y) % 24 < 2:
                color = tuple(int(c * 0.75) for c in base)  # diagonal weave
            elif x % 8 == 0 and y % 8 == 0:
                color = accent  # stitch dots
            pixels.extend((*color, 255))
    return encode_png(size, size, bytes(pixels))


# --------------------------------------------------------------- helpers

def axis_angle(axis: tuple, radians: float) -> tuple:
    length = math.sqrt(sum(c * c for c in axis))
    nx, ny, nz = (c / length for c in axis)
    half = radians * 0.5
    s = math.sin(half)
    return (nx * s, ny * s, nz * s, math.cos(half))


class BinBuilder:
    """Accumulates the BIN chunk and the bufferViews/accessors JSON."""

    def __init__(self) -> None:
        self.blob = bytearray()
        self.buffer_views = []
        self.accessors = []

    def _add_view(self, payload: bytes) -> int:
        while len(self.blob) % 4:
            self.blob.append(0)
        view = {"buffer": 0, "byteOffset": len(self.blob), "byteLength": len(payload)}
        self.blob.extend(payload)
        self.buffer_views.append(view)
        return len(self.buffer_views) - 1

    def add_accessor(self, kind: str, component: int, values, minmax=False) -> int:
        per = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[kind]
        rows = [v if isinstance(v, (tuple, list)) else (v,) for v in values]
        flat = [c for v in rows for c in v]
        fmt = {FLOAT: "f", UINT16: "H", UINT32: "I"}[component]
        payload = struct.pack("<%d%s" % (len(flat), fmt), *flat)
        accessor = {
            "bufferView": self._add_view(payload),
            "componentType": component,
            "count": len(flat) // per,
            "type": kind,
        }
        if minmax:
            accessor["min"] = [min(v[i] for v in rows) for i in range(per)]
            accessor["max"] = [max(v[i] for v in rows) for i in range(per)]
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def add_image_view(self, payload: bytes) -> int:
        return self._add_view(payload)


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


# ------------------------------------------------------------- character

# name, parent name (None = root), local translation (bind pose)
JOINT_DEFS = [
    ("hips", None, (0.0, 0.95, 0.0)),
    ("spine", "hips", (0.0, 0.10, 0.0)),
    ("chest", "spine", (0.0, 0.15, 0.0)),
    ("neck", "chest", (0.0, 0.22, 0.0)),
    ("head", "neck", (0.0, 0.08, 0.0)),
    ("shoulder_l", "chest", (0.07, 0.16, 0.0)),
    ("upper_arm_l", "shoulder_l", (0.11, 0.0, 0.0)),
    ("forearm_l", "upper_arm_l", (0.26, 0.0, 0.0)),
    ("hand_l", "forearm_l", (0.24, 0.0, 0.0)),
    ("shoulder_r", "chest", (-0.07, 0.16, 0.0)),
    ("upper_arm_r", "shoulder_r", (-0.11, 0.0, 0.0)),
    ("forearm_r", "upper_arm_r", (-0.26, 0.0, 0.0)),
    ("hand_r", "forearm_r", (-0.24, 0.0, 0.0)),
    ("thigh_l", "hips", (0.10, -0.03, 0.0)),
    ("shin_l", "thigh_l", (0.0, -0.42, 0.0)),
    ("foot_l", "shin_l", (0.0, -0.42, 0.0)),
    ("thigh_r", "hips", (-0.10, -0.03, 0.0)),
    ("shin_r", "thigh_r", (0.0, -0.42, 0.0)),
    ("foot_r", "shin_r", (0.0, -0.42, 0.0)),
]

J = {name: index for index, (name, _, _) in enumerate(JOINT_DEFS)}
JOINTS = [(name, -1 if parent is None else J[parent], local)
          for name, parent, local in JOINT_DEFS]


def joint_world(index: int) -> tuple:
    name, parent, local = JOINTS[index]
    if parent < 0:
        return local
    px, py, pz = joint_world(parent)
    return (px + local[0], py + local[1], pz + local[2])


def _vec_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _vec_scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def _vec_add(*vs):
    return tuple(sum(c) for c in zip(*vs))


def _vec_norm(a):
    length = math.sqrt(sum(c * c for c in a)) or 1.0
    return _vec_scale(a, 1.0 / length)


def _vec_cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


class Geometry:
    def __init__(self) -> None:
        self.positions = []
        self.normals = []
        self.uvs = []
        self.joints = []
        self.weights = []
        self.indices = []

    def add_quad(self, corners, normal, joint_weights):
        """4 corners CCW seen from the normal side; per-corner (joints, weights)."""
        base = len(self.positions)
        for i, corner in enumerate(corners):
            self.positions.append(corner)
            self.normals.append(normal)
            self.uvs.append([(0, 1), (1, 1), (1, 0), (0, 0)][i])
            jw_joints, jw_weights = joint_weights[i]
            self.joints.append(jw_joints)
            self.weights.append(jw_weights)
        self.indices.extend((base, base + 1, base + 2, base, base + 2, base + 3))

    def add_chain(self, chain, radii, subdivisions=2):
        """Square tube along a joint chain with smooth ring skinning.

        chain: [(world position, joint index), ...]; radii per node. Rings
        between node k and k+1 blend linearly between the two joints, so
        interior vertices carry two influences like a production rig.
        """
        # Sample rings along the chain.
        rings = []  # (position, radius, (joints4, weights4))
        for k in range(len(chain) - 1):
            p0, j0 = chain[k]
            p1, j1 = chain[k + 1]
            r0, r1 = radii[k], radii[k + 1]
            steps = subdivisions if k < len(chain) - 2 else subdivisions + 1
            for step in range(steps):
                s = step / subdivisions
                position = _vec_add(_vec_scale(p0, 1.0 - s), _vec_scale(p1, s))
                radius = r0 * (1.0 - s) + r1 * s
                if j0 == j1 or s == 0.0:
                    influence = ((j0, 0, 0, 0), (1.0, 0.0, 0.0, 0.0))
                elif s == 1.0:
                    influence = ((j1, 0, 0, 0), (1.0, 0.0, 0.0, 0.0))
                else:
                    influence = ((j0, j1, 0, 0), (1.0 - s, s, 0.0, 0.0))
                rings.append((position, radius, influence))

        # Frame: two perpendicular directions to the chain's overall axis.
        axis = _vec_norm(_vec_sub(chain[-1][0], chain[0][0]))
        up = (0.0, 1.0, 0.0) if abs(axis[1]) < 0.9 else (0.0, 0.0, 1.0)
        side_u = _vec_norm(_vec_cross(axis, up))
        side_v = _vec_norm(_vec_cross(axis, side_u))

        def ring_corner(ring, cu, cv):
            position, radius, _ = ring
            return _vec_add(position, _vec_scale(side_u, cu * radius),
                            _vec_scale(side_v, cv * radius))

        # 4 flat-shaded side strips.
        sides = [
            (side_u, (1, -1), (1, 1)),
            (_vec_scale(side_u, -1.0), (-1, 1), (-1, -1)),
            (side_v, (1, 1), (-1, 1)),
            (_vec_scale(side_v, -1.0), (-1, -1), (1, -1)),
        ]
        for normal, (u0, v0), (u1, v1) in sides:
            for k in range(len(rings) - 1):
                near, far = rings[k], rings[k + 1]
                corners = [ring_corner(near, u0, v0), ring_corner(near, u1, v1),
                           ring_corner(far, u1, v1), ring_corner(far, u0, v0)]
                influences = [near[2], near[2], far[2], far[2]]
                self.add_quad(corners, normal, influences)

        # End caps.
        first, last = rings[0], rings[-1]
        cap_corners = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
        self.add_quad([ring_corner(first, u, v) for u, v in reversed(cap_corners)],
                      _vec_scale(axis, -1.0), [first[2]] * 4)
        self.add_quad([ring_corner(last, u, v) for u, v in cap_corners],
                      axis, [last[2]] * 4)


def build_geometry():
    g = Geometry()
    w = joint_world

    # Torso: hips -> spine -> chest -> neck, tapering into the neck.
    g.add_chain([((0.0, 0.86, 0.0), J["hips"]), (w(J["spine"]), J["spine"]),
                 (w(J["chest"]), J["chest"]), (w(J["neck"]), J["neck"])],
                [0.17, 0.16, 0.17, 0.08])
    # Head.
    g.add_chain([(w(J["neck"]), J["neck"]), ((0.0, 1.50, 0.0), J["head"]),
                 ((0.0, 1.66, 0.0), J["head"])],
                [0.07, 0.11, 0.10])
    # Arms (T-pose along X), tapering shoulder -> hand.
    for side, sign in (("l", 1.0), ("r", -1.0)):
        g.add_chain([((sign * 0.14, 1.36, 0.0), J[f"upper_arm_{side}"]),
                     (w(J[f"forearm_{side}"]), J[f"forearm_{side}"]),
                     (w(J[f"hand_{side}"]), J[f"hand_{side}"]),
                     ((sign * 0.80, 1.36, 0.0), J[f"hand_{side}"])],
                    [0.055, 0.05, 0.045, 0.04])
    # Legs, then feet pointing forward (-Z is the character's facing).
    for side, sign in (("l", 1.0), ("r", -1.0)):
        g.add_chain([(w(J[f"thigh_{side}"]), J[f"thigh_{side}"]),
                     (w(J[f"shin_{side}"]), J[f"shin_{side}"]),
                     ((sign * 0.10, 0.10, 0.0), J[f"foot_{side}"])],
                    [0.085, 0.07, 0.06])
        g.add_chain([((sign * 0.10, 0.055, 0.02), J[f"foot_{side}"]),
                     ((sign * 0.10, 0.05, -0.16), J[f"foot_{side}"])],
                    [0.05, 0.045], subdivisions=1)

    return g.positions, g.normals, g.uvs, g.joints, g.weights, g.indices


def clip_channels(name: str):
    """Keyframes per clip: list of (joint, path, [(t, value), ...])."""
    def swing(axis, amplitude, period, phase=0.0, keys=9):
        return [
            (period * k / (keys - 1),
             axis_angle(axis, amplitude * math.sin(2 * math.pi * k / (keys - 1) + phase)))
            for k in range(keys)
        ]

    x, y, z = (1, 0, 0), (0, 1, 0), (0, 0, 1)
    half_pi = math.pi / 2

    if name == "locomotion":
        period = 0.8
        return [
            (J["hips"], "translation",
             [(period * k / 8,
               (0.0, 0.95 + 0.03 * abs(math.sin(2 * math.pi * k / 8)), 0.0))
              for k in range(9)]),
            (J["thigh_l"], "rotation", swing(x, 0.7, period)),
            (J["thigh_r"], "rotation", swing(x, 0.7, period, math.pi)),
            # Knees bend a quarter cycle behind the thigh swing.
            (J["shin_l"], "rotation", swing(x, 0.45, period, half_pi)),
            (J["shin_r"], "rotation", swing(x, 0.45, period, math.pi + half_pi)),
            (J["upper_arm_l"], "rotation", swing(x, 0.4, period, math.pi)),
            (J["upper_arm_r"], "rotation", swing(x, 0.4, period)),
            (J["forearm_l"], "rotation", swing(x, 0.2, period, math.pi + half_pi)),
            (J["forearm_r"], "rotation", swing(x, 0.2, period, half_pi)),
            (J["chest"], "rotation", swing(y, 0.12, period)),
        ]
    if name == "attack":
        return [
            (J["upper_arm_r"], "rotation",
             [(0.0, axis_angle(x, 0.0)), (0.12, axis_angle(x, -2.2)),
              (0.28, axis_angle(x, 0.7)), (0.5, axis_angle(x, 0.0))]),
            (J["forearm_r"], "rotation",
             [(0.0, axis_angle(x, 0.0)), (0.12, axis_angle(x, -0.9)),
              (0.28, axis_angle(x, 0.3)), (0.5, axis_angle(x, 0.0))]),
            (J["chest"], "rotation",
             [(0.0, axis_angle(y, 0.0)), (0.12, axis_angle(y, -0.55)),
              (0.28, axis_angle(y, 0.35)), (0.5, axis_angle(y, 0.0))]),
            (J["hips"], "rotation",
             [(0.0, axis_angle(y, 0.0)), (0.12, axis_angle(y, -0.2)),
              (0.28, axis_angle(y, 0.15)), (0.5, axis_angle(y, 0.0))]),
        ]
    if name == "dodge":
        return [
            (J["hips"], "translation",
             [(0.0, (0.0, 0.95, 0.0)), (0.2, (0.6, 0.8, 0.0)),
              (0.4, (0.6, 0.95, 0.0))]),
            (J["chest"], "rotation",
             [(0.0, axis_angle(z, 0.0)), (0.2, axis_angle(z, 0.5)),
              (0.4, axis_angle(z, 0.0))]),
            (J["thigh_l"], "rotation",
             [(0.0, axis_angle(x, 0.0)), (0.2, axis_angle(x, -0.5)),
              (0.4, axis_angle(x, 0.0))]),
        ]
    if name == "hit_react":
        return [
            (J["chest"], "rotation",
             [(0.0, axis_angle(x, 0.0)), (0.1, axis_angle(x, -0.5)),
              (0.3, axis_angle(x, 0.0))]),
            (J["neck"], "rotation",
             [(0.0, axis_angle(x, 0.0)), (0.1, axis_angle(x, -0.35)),
              (0.3, axis_angle(x, 0.0))]),
            (J["head"], "rotation",
             [(0.0, axis_angle(x, 0.0)), (0.1, axis_angle(x, -0.25)),
              (0.3, axis_angle(x, 0.0))]),
        ]
    if name == "death":
        return [
            (J["hips"], "translation",
             [(0.0, (0.0, 0.95, 0.0)), (0.6, (0.0, 0.4, -0.3)),
              (1.2, (0.0, 0.12, -0.6))]),
            (J["hips"], "rotation",
             [(0.0, axis_angle(x, 0.0)), (0.6, axis_angle(x, -0.9)),
              (1.2, axis_angle(x, -1.5))]),
            (J["chest"], "rotation",
             [(0.0, axis_angle(x, 0.0)), (0.6, axis_angle(x, -0.3)),
              (1.2, axis_angle(x, -0.5))]),
            (J["neck"], "rotation",
             [(0.0, axis_angle(x, 0.0)), (0.6, axis_angle(x, -0.3)),
              (1.2, axis_angle(x, -0.6))]),
        ]
    raise ValueError(name)


CLIPS = ["locomotion", "attack", "dodge", "hit_react", "death"]


def build_character(base: tuple, accent: tuple, metallic: float, roughness: float) -> bytes:
    bin_builder = BinBuilder()
    positions, normals, uvs, joints, weights, indices = build_geometry()

    a_pos = bin_builder.add_accessor("VEC3", FLOAT, positions, minmax=True)
    a_norm = bin_builder.add_accessor("VEC3", FLOAT, normals)
    a_uv = bin_builder.add_accessor("VEC2", FLOAT, uvs)
    a_joints = bin_builder.add_accessor("VEC4", UINT16, joints)
    a_weights = bin_builder.add_accessor("VEC4", FLOAT, weights)
    a_indices = bin_builder.add_accessor("SCALAR", UINT32, indices)

    # Inverse bind matrices: pure translation by -joint_world (column-major).
    ibms = []
    for j in range(len(JOINTS)):
        wx, wy, wz = joint_world(j)
        ibms.append((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -wx, -wy, -wz, 1))
    a_ibm = bin_builder.add_accessor("MAT4", FLOAT, ibms)

    animations = []
    for clip_name in CLIPS:
        samplers, channels = [], []
        for joint, path, keys in clip_channels(clip_name):
            times = [(k[0],) for k in keys]
            values = [k[1] for k in keys]
            kind = "VEC4" if path == "rotation" else "VEC3"
            a_times = bin_builder.add_accessor("SCALAR", FLOAT, times, minmax=True)
            a_values = bin_builder.add_accessor(kind, FLOAT, values)
            samplers.append({"input": a_times, "output": a_values,
                             "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1,
                             "target": {"node": 1 + joint, "path": path}})
        animations.append({"name": clip_name, "samplers": samplers,
                           "channels": channels})

    png = character_texture(base, accent)
    image_view = bin_builder.add_image_view(png)

    # Node 0: character (mesh + skin). Nodes 1..7: joints.
    nodes = [{"name": "character", "mesh": 0, "skin": 0}]
    for index, (name, parent, local) in enumerate(JOINTS):
        node = {"name": name, "translation": list(local)}
        children = [1 + c for c, (_, p, _) in enumerate(JOINTS) if p == index]
        if children:
            node["children"] = children
        nodes.append(node)
        del parent  # hierarchy expressed through children lists

    gltf = {
        "asset": {"version": "2.0", "generator": "hue generate_characters"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": nodes,
        "meshes": [{"primitives": [{
            "attributes": {"POSITION": a_pos, "NORMAL": a_norm, "TEXCOORD_0": a_uv,
                           "JOINTS_0": a_joints, "WEIGHTS_0": a_weights},
            "indices": a_indices, "material": 0}]}],
        "skins": [{"joints": [1 + j for j in range(len(JOINTS))],
                   "inverseBindMatrices": a_ibm, "skeleton": 1}],
        "animations": animations,
        "materials": [{"pbrMetallicRoughness": {
            "baseColorTexture": {"index": 0},
            "metallicFactor": metallic, "roughnessFactor": roughness}}],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9729, "minFilter": 9987,
                      "wrapS": 10497, "wrapT": 10497}],
        "images": [{"bufferView": image_view, "mimeType": "image/png"}],
        "buffers": [{"byteLength": len(bin_builder.blob)}],
        "bufferViews": bin_builder.buffer_views,
        "accessors": bin_builder.accessors,
    }
    return glb_bytes(gltf, bin_builder.blob)


# ----------------------------------------------------------- fuzz seeds

def build_min_skinned(corrupt_joint: bool) -> bytes:
    """Single skinned triangle, 2 joints, 1 clip: compact corpus seed."""
    b = BinBuilder()
    a_pos = b.add_accessor("VEC3", FLOAT,
                           [(0, 0, 0), (1, 0, 0), (0, 1, 0)], minmax=True)
    bad = 9 if corrupt_joint else 1
    a_joints = b.add_accessor("VEC4", UINT16, [(0, bad, 0, 0)] * 3)
    a_weights = b.add_accessor("VEC4", FLOAT, [(0.5, 0.5, 0.0, 0.0)] * 3)
    a_indices = b.add_accessor("SCALAR", UINT32, [0, 1, 2])
    a_ibm = b.add_accessor("MAT4", FLOAT,
                           [(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)] * 2)
    a_times = b.add_accessor("SCALAR", FLOAT, [(0.0,), (1.0,)], minmax=True)
    a_values = b.add_accessor("VEC4", FLOAT, [(0, 0, 0, 1), (0, 0.707, 0, 0.707)])

    gltf = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            {"mesh": 0, "skin": 0},
            {"name": "root", "children": [2]},
            {"name": "tip", "translation": [0, 1, 0]},
        ],
        "meshes": [{"primitives": [{
            "attributes": {"POSITION": a_pos, "JOINTS_0": a_joints,
                           "WEIGHTS_0": a_weights},
            "indices": a_indices}]}],
        "skins": [{"joints": [1, 2], "inverseBindMatrices": a_ibm}],
        "animations": [{"name": "wave",
                        "samplers": [{"input": a_times, "output": a_values,
                                      "interpolation": "LINEAR"}],
                        "channels": [{"sampler": 0,
                                      "target": {"node": 2, "path": "rotation"}}]}],
        "buffers": [{"byteLength": len(b.blob)}],
        "bufferViews": b.buffer_views,
        "accessors": b.accessors,
    }
    return glb_bytes(gltf, b.blob)


def build_min_textured() -> bytes:
    """Textured triangle with a 4x4 PNG: texture-decode corpus seed."""
    b = BinBuilder()
    a_pos = b.add_accessor("VEC3", FLOAT,
                           [(0, 0, 0), (1, 0, 0), (0, 1, 0)], minmax=True)
    a_uv = b.add_accessor("VEC2", FLOAT, [(0, 0), (1, 0), (0, 1)])
    a_indices = b.add_accessor("SCALAR", UINT32, [0, 1, 2])
    png = encode_png(4, 4, bytes(
        c for i in range(16) for c in ((255, 0, 128, 255) if i % 2 else (0, 255, 64, 255))
    ))
    image_view = b.add_image_view(png)

    gltf = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": [{
            "attributes": {"POSITION": a_pos, "TEXCOORD_0": a_uv},
            "indices": a_indices, "material": 0}]}],
        "materials": [{"pbrMetallicRoughness": {
            "baseColorTexture": {"index": 0},
            "metallicFactor": 0.5, "roughnessFactor": 0.5}}],
        "textures": [{"source": 0}],
        "images": [{"bufferView": image_view, "mimeType": "image/png"}],
        "buffers": [{"byteLength": len(b.blob)}],
        "bufferViews": b.buffer_views,
        "accessors": b.accessors,
    }
    return glb_bytes(gltf, b.blob)


def main() -> None:
    MODELS_DIR.mkdir(parents=True, exist_ok=True)
    CORPUS_DIR.mkdir(parents=True, exist_ok=True)

    player = build_character(base=(58, 100, 190), accent=(224, 200, 120),
                             metallic=0.1, roughness=0.7)
    enemy = build_character(base=(170, 52, 48), accent=(40, 36, 34),
                            metallic=0.2, roughness=0.55)
    (MODELS_DIR / "player.glb").write_bytes(player)
    (MODELS_DIR / "enemy.glb").write_bytes(enemy)
    print(f"player.glb: {len(player)} bytes")
    print(f"enemy.glb: {len(enemy)} bytes")

    seeds = {
        "skinned_min.glb": build_min_skinned(corrupt_joint=False),
        "skinned_bad_joint.glb": build_min_skinned(corrupt_joint=True),
        "textured_min.glb": build_min_textured(),
        "skinned_character.glb": player,
    }
    for name, blob in seeds.items():
        (CORPUS_DIR / name).write_bytes(blob)
        print(f"corpus {name}: {len(blob)} bytes")


if __name__ == "__main__":
    main()
