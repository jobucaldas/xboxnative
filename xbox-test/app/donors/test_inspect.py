# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("donor_inspect", Path(__file__).with_name("inspect.py"))
donor_inspect = importlib.util.module_from_spec(spec)
spec.loader.exec_module(donor_inspect)


class DonorProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.url, self.pin = "https://example.invalid/wine.tar.xz", "a" * 64
        (self.root / "SOURCE.txt").write_text(f"source: {self.url} (sha256 {self.pin})\n")
        self.rows = []
        for name in donor_inspect.DONORS:
            data = name.encode()
            (self.root / name).write_bytes(data)
            self.rows.append(f"{hashlib.sha256(data).hexdigest()}  {name}")
        self.manifest()

    def manifest(self):
        (self.root / "SHA256SUMS").write_text("\n".join(self.rows) + "\n")

    def check(self):
        return donor_inspect.checksums(self.root, self.url, self.pin)

    def test_complete_manifest(self):
        self.assertEqual(set(self.check()), set(donor_inspect.DONORS))

    def test_changed_binary(self):
        (self.root / "hid.dll").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
            self.check()

    def test_wrong_source_pin(self):
        (self.root / "SOURCE.txt").write_text(f"source: {self.url} (sha256 {'b' * 64})\n")
        with self.assertRaisesRegex(ValueError, "source pin"):
            self.check()

    def test_missing_duplicate_or_extra_donor(self):
        original = self.rows[:]
        for rows in (original[:-1], original + original[:1], original + [f"{'c' * 64}  extra.dll"]):
            with self.subTest(rows=len(rows)):
                self.rows = rows
                self.manifest()
                with self.assertRaises(ValueError):
                    self.check()

    def test_manifest_path_not_a_donor(self):
        self.rows[0] = f"{'c' * 64}  ../outside.dll"
        self.manifest()
        with self.assertRaises(ValueError):
            self.check()


if __name__ == "__main__":
    unittest.main()
