# tests/test_package.py
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("hue_package", Path(__file__).resolve().parents[1] / "tools/package.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)

class PackageTests(unittest.TestCase):
    def test_manifest_detects_missing_changed_and_extra_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in package.REQUIRED | {"hue"} | {"shaders/" + n for n in package.SHADERS}:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fixture")
            manifest = {"schema": 1, "files": package.inventory(root)}
            (root / "manifest.json").write_text(json.dumps(manifest))
            self.assertEqual(package.verify(root), manifest)
            (root / "hue").write_bytes(b"changed")
            with self.assertRaises(ValueError): package.verify(root)
            (root / "hue").write_bytes(b"fixture")
            (root / "unexpected.txt").write_text("extra")
            with self.assertRaises(ValueError): package.verify(root)
            (root / "unexpected.txt").unlink()
            (root / "assets/config/tuning.json").unlink()
            with self.assertRaises(ValueError): package.verify(root)
    def test_refuses_existing_output_and_malformed_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(ValueError): package.package(root / "missing-build", root, "cmake")
            (root / "manifest.json").write_text("[]")
            with self.assertRaisesRegex(ValueError, "invalid manifest schema"): package.verify(root)

    def test_rejects_sanitizer_files_and_missing_crt(self):
        files = {name: {} for name in package.REQUIRED | {"hue.exe"} | {"shaders/" + n for n in package.SHADERS}}
        with self.assertRaises(ValueError): package.check_required(files)
        files.update({name: {} for name in ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")})
        package.check_required(files)
        files["clang_rt.asan.dll"] = {}
        with self.assertRaises(ValueError): package.check_required(files)

if __name__ == "__main__": unittest.main()
