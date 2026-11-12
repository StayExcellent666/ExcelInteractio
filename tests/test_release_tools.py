"""Checks release metadata and staging using disposable synthetic PE fixtures."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("release_tools", root / "scripts" / "release_tools.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)

def pe(path):
    data = bytearray(128)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 64)
    data[64:68] = b"PE\0\0"
    struct.pack_into("<H", data, 68, 0x8664)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)

class ReleaseTests(unittest.TestCase):
    def setUp(self):
        (root / "build").mkdir(exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=root / "build")
        self.root = Path(self.tmp.name)
        self.stage = self.root / "stage"
        self.dist = self.root / "dist"
        self.dist.mkdir()
        for name in release.REQUIRED:
            path = self.stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            pe(path) if path.suffix in (".dll", ".exe") else path.write_text("fixture")
        pe(self.dist / "ExcelInteractio-0.1.0-beta.1-windows-x64-setup.exe")

    def tearDown(self):
        self.tmp.cleanup()

    def test_manifest_and_tamper(self):
        result = release.package(self.stage, self.dist, "0.1.0-beta.1", "StayExcellent666/ExcelInteractio", "a" * 40)
        self.assertEqual(result["obs"]["maximum_exclusive"], "34.0.0")
        release.validate_bundle(self.dist, "0.1.0-beta.1", "StayExcellent666/ExcelInteractio", "a" * 40)
        (self.dist / result["assets"]["installer"]["name"]).write_bytes(b"changed")
        with self.assertRaises(ValueError):
            release.validate_bundle(self.dist, "0.1.0-beta.1", "StayExcellent666/ExcelInteractio", "a" * 40)

    def test_foreign_repository_and_missing_asset(self):
        with self.assertRaises(ValueError):
            release.package(self.stage, self.dist, "0.1.0-beta.1", "attacker/ExcelInteractio", "a" * 40)
        (self.stage / release.REQUIRED[-1]).unlink()
        with self.assertRaises(ValueError):
            release.stage_files(self.stage)

    def test_payload_escape_rejected(self):
        for name in ("../file", "C:/file", release.DATA + "../other", release.DATA + "a:stream"):
            self.assertFalse(release.safe_payload_name(name))

if __name__ == "__main__":
    unittest.main()
