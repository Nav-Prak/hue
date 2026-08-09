#!/usr/bin/env python3
"""Write a tiny seed corpus for fuzz_gltf into tools/fuzz/corpus/gltf.

Seeds are intentionally small: one valid triangle GLB, a few structured
near-misses, and a couple of garbage blobs so libFuzzer has something to
mutate on day one of CI.
"""

from __future__ import annotations

import struct
from pathlib import Path


def pad4(n: int) -> int:
    return (n + 3) & ~3


def write_glb(path: Path, json_text: str, bin_bytes: bytes = b"") -> None:
    json_raw = json_text.encode("utf-8")
    json_padded = pad4(len(json_raw))
    bin_padded = pad4(len(bin_bytes))
    total = 12 + 8 + json_padded + 8 + bin_padded

    chunks = [
        struct.pack("<III", 0x46546C67, 2, total),
        struct.pack("<II", json_padded, 0x4E4F534A),
        json_raw + b" " * (json_padded - len(json_raw)),
        struct.pack("<II", bin_padded, 0x004E4942),
        bin_bytes + b"\x00" * (bin_padded - len(bin_bytes)),
    ]
    path.write_bytes(b"".join(chunks))


def main() -> None:
    out = Path(__file__).resolve().parent / "corpus" / "gltf"
    out.mkdir(parents=True, exist_ok=True)

    positions = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0)
    indices_ok = struct.pack("<3I", 0, 1, 2)
    indices_oob = struct.pack("<3I", 0, 1, 99)
    bin_ok = positions + indices_ok
    bin_oob = positions + indices_oob

    triangle_json = (
        '{"asset":{"version":"2.0"},'
        '"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],'
        '"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],'
        '"accessors":['
        '{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",'
        '"min":[0,0,0],"max":[1,1,0]},'
        '{"bufferView":1,"componentType":5125,"count":3,"type":"SCALAR"}],'
        '"bufferViews":['
        '{"buffer":0,"byteOffset":0,"byteLength":36},'
        '{"buffer":0,"byteOffset":36,"byteLength":12}],'
        '"buffers":[{"byteLength":48}]}'
    )

    write_glb(out / "triangle_ok.glb", triangle_json, bin_ok)
    write_glb(out / "triangle_oob_index.glb", triangle_json, bin_oob)
    write_glb(out / "truncated_bin.glb", triangle_json, positions[:12])

    external_json = (
        '{"asset":{"version":"2.0"},'
        '"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],'
        '"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],'
        '"accessors":[{"bufferView":0,"componentType":5126,"count":3,'
        '"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],'
        '"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],'
        '"buffers":[{"byteLength":36,"uri":"evil.bin"}]}'
    )
    # JSON-only GLB (no BIN chunk) for the external-uri case.
    json_raw = external_json.encode("utf-8")
    json_padded = pad4(len(json_raw))
    total = 12 + 8 + json_padded
    (out / "external_uri.glb").write_bytes(
        struct.pack("<III", 0x46546C67, 2, total)
        + struct.pack("<II", json_padded, 0x4E4F534A)
        + json_raw
        + b" " * (json_padded - len(json_raw))
    )

    (out / "empty.bin").write_bytes(b"")
    (out / "garbage.txt").write_bytes(b"not a gltf file whatsoever\x00\xff\xfe")
    (out / "truncated_header.glb").write_bytes(struct.pack("<II", 0x46546C67, 2))

    print(f"wrote {len(list(out.iterdir()))} seeds to {out}")


if __name__ == "__main__":
    main()
