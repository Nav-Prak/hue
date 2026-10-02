# tests/package_smoke.py
"""Verify a copied package outside the checkout with no developer toolchain PATH."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

spec = importlib.util.spec_from_file_location("hue_package", Path(__file__).resolve().parents[1] / "tools/package.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)

def main():
    source = Path(sys.argv[1]).resolve()
    manifest = package.verify(source)
    with tempfile.TemporaryDirectory(prefix="hue-package-") as directory:
        root = Path(directory) / "runtime"
        shutil.copytree(source, root)
        package.verify(root)
        executable = root / ("hue.exe" if os.name == "nt" else "hue")
        environment = dict(os.environ)
        if os.name == "nt":
            system = Path(os.environ["SystemRoot"]) # Windows os.environ is case-insensitive
            environment["PATH"] = os.pathsep.join((str(system / "System32"), str(system)))
        result = subprocess.run([str(executable), "--encounter-smoke", "--baseline-hud", "--require-renderer"],
                                cwd=directory, env=environment, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, errors="replace", timeout=170)
        print(result.stdout)
        if result.returncode:
            return result.returncode
        if "encounter_smoke PASS" not in result.stdout or result.stdout.count("fight_summary outcome=") != 20:
            raise RuntimeError("missing twenty completed lifecycle outcomes")
        if result.stdout.count("fight_summary outcome=VICTORY") != 10 or result.stdout.count("fight_summary outcome=DEFEAT") != 10:
            raise RuntimeError("both outcomes were not covered")
        package.verify(root) # gameplay must not mutate shipped files
        print(f"PACKAGE_SMOKE_PASS revision={manifest['source_revision']} dirty={manifest['source_dirty']}")
        print("Actual Vulkan, twenty injected outcomes/restarts, executable-relative assets, restricted PATH; NOT clean-machine acceptance.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
