# tools/package.py
"""Build a fresh local Hue runtime package; never publish or overwrite an artifact."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

SHADERS = {name + ".spv" for name in ("triangle.vert", "triangle.frag", "mesh.vert",
           "skinned_mesh.vert", "mesh.frag", "hud.vert", "hud.frag")}
ACCEPTANCE = {
    "status": "owner-waived-external-gates",
    "decision_date": "2026-10-14",
    "scope": "Windows two-attack v1 candidate",
    "externally_verified": False,
    "waived_requirements": ["external playtest acceptance", "clean target machine",
                            "physical gamepad acceptance", "Linux CI as a Windows completion gate"],
    "deferred_evidence": ["sustained 24-48-hour fuzz campaign", "saved Tracy CPU/GPU capture",
                          "interactive balance improvement"],
}
REQUIRED = {"assets/models/player.glb", "assets/models/enemy.glb",
    "assets/models/player.events.json", "assets/models/enemy.events.json",
    "assets/models/LICENSE-KayKit.txt", "assets/config/arena.json", "assets/config/tuning.json",
    "README.txt", "third-party-notices.txt", "licenses/spirv-tools/LICENSE",
    "licenses/tracy/LICENSE", "licenses/tracy/libbacktrace/LICENSE",
    "licenses/glfw/LICENSE.md", "licenses/cgltf/LICENSE", "licenses/stb/LICENSE",
    "licenses/joltphysics/LICENSE", "licenses/volk/LICENSE.md", "licenses/vma/LICENSE.txt",
    "licenses/vulkan-headers/LICENSE.md", "licenses/vulkan-headers/Apache-2.0.txt",
    "licenses/vulkan-headers/MIT.txt", "licenses/spirv-headers/LICENSE", "licenses/glslang/LICENSE.txt"}

def inventory(root):
    result = {}
    total = 0
    for visited, path in enumerate(root.rglob("*")):
        if visited >= 512:
            raise ValueError("unexpected package directory/file count")
        if path.is_symlink():
            raise ValueError(f"package may not contain symlinks: {path.name}")
        if not path.is_file() or path == root / "manifest.json":
            continue
        size = path.stat().st_size
        total += size
        if size > 64 * 1024 * 1024 or total > 256 * 1024 * 1024 or len(result) >= 256:
            raise ValueError("unexpected package size/file count")
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        result[path.relative_to(root).as_posix()] = {"bytes": size, "sha256": digest}
    return dict(sorted(result.items()))

def check_required(files, acceptance=None):
    # Earlier schema-1 review packages predate the owner's waiver and notes.
    if isinstance(acceptance, dict) and acceptance.get("status") == ACCEPTANCE["status"] and "RELEASE-NOTES.md" not in files:
        raise ValueError("owner-waived candidate must include RELEASE-NOTES.md")
    missing = REQUIRED - files.keys()
    if missing:
        raise ValueError(f"missing runtime inputs/notices: {sorted(missing)}")
    shaders = {name.removeprefix("shaders/") for name in files if name.startswith("shaders/")}
    if shaders != SHADERS:
        raise ValueError("expected exactly seven compiled shaders")
    if "hue.exe" in files:
        missing_crt = {"msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"} - files.keys()
        if missing_crt:
            raise ValueError(f"missing Windows Release runtime DLLs: {sorted(missing_crt)}")
    elif "hue" not in files:
        raise ValueError("game executable missing")
    if any("asan" in name.lower() or name.endswith((".pdb", ".lib")) for name in files):
        raise ValueError("debug/sanitizer/development files do not belong in this package")

def verify(root):
    root = Path(root).resolve()
    manifest_path = root / "manifest.json"
    if manifest_path.is_symlink() or manifest_path.stat().st_size > 128 * 1024:
        raise ValueError("invalid manifest file")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or type(manifest.get("schema")) is not int or manifest.get("schema") != 1:
        raise ValueError("invalid manifest schema")
    files = inventory(root)
    check_required(files, manifest.get("acceptance"))
    if manifest.get("files") != files:
        raise ValueError("package integrity mismatch")
    return manifest

def package(build, output, cmake, require_clean=False):
    build, output = build.resolve(), output.resolve()
    archive = output.with_suffix(".zip")
    if output.exists() or archive.exists():
        raise ValueError("output directory/archive already exists; choose a fresh destination")
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    entries = {line.split(":", 1)[0]: line.split("=", 1)[1]
               for line in cache.splitlines() if not line.startswith(("//", "#")) and ":" in line and "=" in line}
    if entries.get("HUE_SANITIZE", "OFF").upper() not in ("OFF", "FALSE", "0"):
        raise ValueError("sanitized builds are not distributable packages")
    source = Path(__file__).resolve().parents[1]
    if Path(entries.get("CMAKE_HOME_DIRECTORY", "")).resolve() != source:
        raise ValueError("build tree belongs to a different source checkout")
    if not entries.get("CMAKE_CONFIGURATION_TYPES") and entries.get("CMAKE_BUILD_TYPE") != "Release":
        raise ValueError("single-configuration build must be configured as Release")
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=source, text=True).strip())
    if require_clean and dirty:
        raise ValueError("candidate requires a clean source checkout; commit reviewed changes first")
    # Build before install so stale staging output is not mistaken for this revision.
    subprocess.run([cmake, "--build", str(build), "--config", "Release", "--target", "hue_game"], check=True)
    output.mkdir(parents=True)
    subprocess.run([cmake, "--install", str(build), "--config", "Release", "--component", "HueRuntime", "--prefix", str(output)], check=True)
    files = inventory(output)
    check_required(files, ACCEPTANCE)
    final_revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    final_dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=source, text=True).strip())
    if final_revision != revision or final_dirty != dirty:
        raise ValueError("source revision/dirty status changed while packaging; choose a fresh destination and retry")
    manifest = {"schema": 1, "project": "Hue", "configuration": "Release",
                "source_revision": revision, "source_dirty": dirty,
                "acceptance": ACCEPTANCE, "files": files}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    verify(output)
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as zipped:
        for path in sorted(output.rglob("*")):
            if path.is_file():
                zipped.write(path, path.relative_to(output))
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    print(json.dumps({"directory": str(output), "archive": str(archive), "sha256": digest,
                      "files": len(files), "source_dirty": dirty}, indent=2))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--verify", type=Path)
    parser.add_argument("--require-clean", action="store_true", help="refuse uncommitted source changes for a candidate")
    args = parser.parse_args()
    try:
        if args.verify:
            verify(args.verify)
            print("package integrity verified (not clean-machine acceptance)")
        elif args.build and args.output:
            package(args.build, args.output, args.cmake, args.require_clean)
        else:
            parser.error("use --verify DIRECTORY or --build BUILD --output FRESH_DIRECTORY")
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        parser.exit(1, f"package failed: {exc}\n")

if __name__ == "__main__": main()
