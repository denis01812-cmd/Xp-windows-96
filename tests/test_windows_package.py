"""Check the Windows download without requiring Windows, QEMU, or network access."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

from tools.build import create_image
from tools.package_windows import PackageError, package


class WindowsPackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "build"
        self.build.mkdir()
        self.destination = self.root / "xp96-windows"
        self.image = create_image(bytes(510) + b"\x55\xaa", b"loader", b"kernel")
        (self.build / "xp96.img").write_bytes(self.image)
        self.manifest = {
            "image_bytes": len(self.image),
            "sha256": hashlib.sha256(self.image).hexdigest(),
        }
        self.write_manifest()

    def write_manifest(self):
        (self.build / "manifest.json").write_text(json.dumps(self.manifest), encoding="utf-8")

    def test_archive_is_complete_and_contains_no_unrelated_files(self):
        self.destination.mkdir()
        (self.destination / "local-private.txt").write_text("must not be packaged")
        archive = package(self.build, self.destination)
        expected = {
            "xp96.img", "manifest.json", "Start-XP96.bat", "START-HERE.md", "tools/run-windows.ps1"
        }
        with zipfile.ZipFile(archive) as packed:
            self.assertEqual(set(packed.namelist()), expected)
            self.assertEqual(packed.read("xp96.img"), self.image)
            self.assertIn("Windows", packed.read("START-HERE.md").decode("utf-8"))
            self.assertEqual(packed.testzip(), None)
            for name in expected:
                self.assertEqual(packed.read(name), (self.destination / name).read_bytes())

    def test_windows_line_endings_and_powershell_bom(self):
        package(self.build, self.destination)
        for name in ("Start-XP96.bat", "tools/run-windows.ps1"):
            data = (self.destination / name).read_bytes()
            self.assertIn(b"\r\n", data)
            self.assertNotIn(b"\n", data.replace(b"\r\n", b""))
            self.assertNotIn(b"\x00", data)
        self.assertTrue((self.destination / "tools/run-windows.ps1").read_bytes().startswith(b"\xef\xbb\xbf"))
        self.assertFalse((self.destination / "Start-XP96.bat").read_bytes().startswith(b"\xef\xbb\xbf"))

    def test_hash_mismatch_is_rejected_before_writing_output(self):
        self.manifest["sha256"] = "0" * 64
        self.write_manifest()
        with self.assertRaisesRegex(PackageError, "SHA256"):
            package(self.build, self.destination)
        self.assertFalse(self.destination.exists())

    def test_size_mismatch_is_rejected(self):
        self.manifest["image_bytes"] = 12
        self.write_manifest()
        with self.assertRaisesRegex(PackageError, "size"):
            package(self.build, self.destination)

    def test_invalid_boot_signature_is_rejected(self):
        (self.build / "xp96.img").write_bytes(bytes(1024))
        with self.assertRaisesRegex(PackageError, "boot signature"):
            package(self.build, self.destination)

    def test_invalid_manifest_is_rejected(self):
        for data in (b"not json", b"[]", b"null", b"\xff"):
            with self.subTest(data=data):
                (self.build / "manifest.json").write_bytes(data)
                with self.assertRaises(PackageError):
                    package(self.build, self.destination)

    def test_missing_image_is_reported(self):
        (self.build / "xp96.img").unlink()
        with self.assertRaises(FileNotFoundError):
            package(self.build, self.destination)


if __name__ == "__main__":
    unittest.main()
