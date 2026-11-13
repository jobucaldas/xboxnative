# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import importlib.util
from pathlib import Path
import tempfile
from types import SimpleNamespace
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

    def test_source_patch_digest(self):
        patch = self.root / "0001-pe-rodata.patch"
        patch.write_bytes(b"selected source patch")
        digest = hashlib.sha256(patch.read_bytes()).hexdigest()
        source = self.root / "SOURCE.txt"
        source.write_text(source.read_text() + f"Patch: 0001-pe-rodata.patch sha256 {digest}\n")
        self.assertEqual(set(donor_inspect.checksums(self.root, self.url, self.pin, digest)), set(donor_inspect.DONORS))
        patch.write_bytes(b"changed patch")
        with self.assertRaisesRegex(ValueError, "source patch"):
            donor_inspect.checksums(self.root, self.url, self.pin, digest)

    def graphics_tree(self):
        """A donor dir whose graphics/ set is complete and correctly hashed.
        Returns the 0002 patch digest the tree's SOURCE.txt records."""
        self.source_patch()
        graphics = self.root / "graphics"
        graphics.mkdir()
        rows = []
        for name in donor_inspect.GRAPHICS:
            data = name.encode()
            (graphics / name).write_bytes(data)
            rows.append(f"{hashlib.sha256(data).hexdigest()}  {name}")
        (graphics / "SHA256SUMS").write_text("\n".join(rows) + "\n")
        return hashlib.sha256((self.root / "0002-d3dcompiler-static-vkd3d.patch").read_bytes()).hexdigest()

    def source_patch(self):
        patch = self.root / "0002-d3dcompiler-static-vkd3d.patch"
        patch.write_bytes(b"static vkd3d patch")
        digest = hashlib.sha256(patch.read_bytes()).hexdigest()
        source = self.root / "SOURCE.txt"
        text = source.read_text()
        if "0002-d3dcompiler" not in text:
            source.write_text(text + f"Patch: 0002-d3dcompiler-static-vkd3d.patch sha256 {digest}\n")

    def test_graphics_hashes_verified(self):
        digest = self.graphics_tree()
        self.assertEqual(
            set(donor_inspect.checksums(self.root, self.url, self.pin, patch_0002_sha256=digest, graphics=donor_inspect.GRAPHICS)),
            set(donor_inspect.DONORS))

    def test_graphics_tampered_binary(self):
        digest = self.graphics_tree()
        (self.root / "graphics" / "d3dcompiler_47.dll").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
            donor_inspect.checksums(self.root, self.url, self.pin, patch_0002_sha256=digest, graphics=donor_inspect.GRAPHICS)

    def test_graphics_donor_not_listed(self):
        digest = self.graphics_tree()
        sums = self.root / "graphics" / "SHA256SUMS"
        sums.write_text(sums.read_text().replace(donor_inspect.GRAPHICS[0], "other.dll"))
        with self.assertRaisesRegex(ValueError, "graphics checksum manifest"):
            donor_inspect.checksums(self.root, self.url, self.pin, patch_0002_sha256=digest, graphics=donor_inspect.GRAPHICS)

    def test_graphics_patch_mismatch(self):
        self.graphics_tree()
        (self.root / "0002-d3dcompiler-static-vkd3d.patch").write_bytes(b"changed patch")
        with self.assertRaisesRegex(ValueError, "static-vkd3d patch"):
            donor_inspect.checksums(self.root, self.url, self.pin, graphics=donor_inspect.GRAPHICS)

    def test_packaged_manifest_covers_every_donor(self):
        """The layout's merged manifest must list the base and graphics sets
        together: build.sh concatenates SHA256SUMS and graphics/SHA256SUMS, and
        a package missing one set's hashes would ship unverified DLLs."""
        self.graphics_tree()
        merged = sorted(
            (self.root / "SHA256SUMS").read_text().splitlines()
            + (self.root / "graphics" / "SHA256SUMS").read_text().splitlines())
        names = {line.split("  ", 1)[1] for line in merged if line}
        self.assertEqual(names, set(donor_inspect.DONORS) | set(donor_inspect.GRAPHICS))

    def test_load_config_build_gate(self):
        data = bytearray((0x140).to_bytes(4, "little") + bytes(316))
        directory = SimpleNamespace(VirtualAddress=0x1000, Size=0x140)
        section = SimpleNamespace(VirtualAddress=0x1000, SizeOfRawData=0x140, Characteristics=0x40000040)
        pe = SimpleNamespace(OPTIONAL_HEADER=SimpleNamespace(DATA_DIRECTORY=[None]*10+[directory], DllCharacteristics=0),
                             get_section_by_rva=lambda rva: section,
                             get_data=lambda rva, size: bytes(data[:size]))
        donor_inspect.validate_load_config(pe, "fixture.dll")
        mutations = [(section, "Characteristics", 0xC0000040),
                     (section, "Characteristics", 0x60000040),
                     (section, "SizeOfRawData", 319),
                     (directory, "Size", 319),
                     (pe.OPTIONAL_HEADER, "DllCharacteristics", 0x4000)]
        for obj, field, value in mutations:
            with self.subTest(field=field, value=value):
                previous = getattr(obj, field)
                setattr(obj, field, value)
                with self.assertRaisesRegex(ValueError, "load configuration"):
                    donor_inspect.validate_load_config(pe, "fixture.dll")
                setattr(obj, field, previous)
        data[0x58] = 1
        with self.assertRaisesRegex(ValueError, "load configuration"):
            donor_inspect.validate_load_config(pe, "fixture.dll")


if __name__ == "__main__":
    unittest.main()
