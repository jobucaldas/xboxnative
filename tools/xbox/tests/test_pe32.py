"""Exercise the exact portable inspection mapper used by launcher Play.
Real-game imports are independently compared with pefile, not mapper output.
"""
import pathlib
import struct
import subprocess
import tempfile
import unittest
import pefile

ROOT = pathlib.Path(__file__).resolve().parents[3]
GAMES = pathlib.Path('/workspace/x64-run/runs/wgap/pe')

class Mapper(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = pathlib.Path(cls.tmp.name) / 'mapper'
        subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(ROOT/'tools/xbox/tests/pe32-map.cpp'), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_map(self, path):
        return subprocess.run([str(self.exe), str(path)], text=True, capture_output=True)

    def test_real_games_and_imports(self):
        for name in ['spacewar/SteamworksExample.exe', 'limbo/limbo.exe']:
            with self.subTest(game=name):
                path = GAMES/name
                self.assertTrue(path.is_file(), f'required acceptance input missing: {path}')
                p = self.run_map(path)
                self.assertEqual(p.returncode, 0, p.stderr)
                self.assertIn('reloc-errors=0', p.stderr)
                pe = pefile.PE(str(path))
                expected = set()
                for table in ['DIRECTORY_ENTRY_IMPORT', 'DIRECTORY_ENTRY_DELAY_IMPORT']:
                    for d in getattr(pe, table, []):
                        for i in d.imports:
                            expected.add(d.dll.decode().lower()+','+(
                                '#'+str(i.ordinal) if i.import_by_ordinal else i.name.decode()))
                self.assertEqual(set(p.stdout.splitlines()), expected)
                if 'spacewar' in name:
                    self.assertIn('relocations=7887', p.stderr)
                else:
                    self.assertEqual(pe.OPTIONAL_HEADER.DATA_DIRECTORY[5].Size, 0)
                    self.assertIn('relocations=0', p.stderr)
                pe.close()

    def mutate(self, offset, value, message):
        path = GAMES/'spacewar/SteamworksExample.exe'
        data = bytearray(path.read_bytes())
        data[offset:offset+len(value)] = value
        bad = pathlib.Path(self.tmp.name)/'bad.exe'
        bad.write_bytes(data)
        p = self.run_map(bad)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn(message, p.stderr)

    def test_relocation_values_and_zero_fill(self):
        path = GAMES/'spacewar/SteamworksExample.exe'
        dump = pathlib.Path(self.tmp.name)/'mapped-data.bin'
        p = subprocess.run([str(self.exe), str(path), str(dump)], capture_output=True)
        self.assertEqual(p.returncode, 0, p.stderr)
        pe = pefile.PE(str(path))
        original = pe.get_memory_mapped_image()
        mapped = dump.read_bytes()
        delta = 0x10000000-pe.OPTIONAL_HEADER.ImageBase
        tested = 0
        for block in pe.DIRECTORY_ENTRY_BASERELOC:
            for entry in block.entries:
                if entry.type == 0:
                    continue
                self.assertEqual(entry.type, 3)
                before = struct.unpack_from('<I', original, entry.rva)[0]
                after = struct.unpack_from('<I', mapped, entry.rva)[0]
                self.assertEqual(after, (before+delta) & 0xffffffff)
                tested += 1
        self.assertEqual(tested, 7887)
        for section in pe.sections:
            start = section.VirtualAddress+section.SizeOfRawData
            end = section.VirtualAddress+section.Misc_VirtualSize
            if end > start:
                self.assertEqual(mapped[start:end], bytes(end-start))
        pe.close()

    def test_bad_reloc_type_rejected(self):
        pe = pefile.PE(str(GAMES/'spacewar/SteamworksExample.exe'))
        pos = pe.get_offset_from_rva(pe.OPTIONAL_HEADER.DATA_DIRECTORY[5].VirtualAddress)+8
        self.mutate(pos, struct.pack('<H', 0xf000), 'unsupported relocation type')
        pe.close()

    def test_bad_reloc_length_rejected(self):
        pe = pefile.PE(str(GAMES/'spacewar/SteamworksExample.exe'))
        pos = pe.get_offset_from_rva(pe.OPTIONAL_HEADER.DATA_DIRECTORY[5].VirtualAddress)+4
        self.mutate(pos, struct.pack('<I', 7), 'relocation block length')
        pe.close()

    def test_bad_machine_rejected(self):
        pe = pefile.PE(str(GAMES/'spacewar/SteamworksExample.exe'))
        self.mutate(pe.DOS_HEADER.e_lfanew+4, b'\x64\x86', 'requires i386/PE32')
        pe.close()

    def test_section_bounds_rejected(self):
        pe = pefile.PE(str(GAMES/'spacewar/SteamworksExample.exe'))
        self.mutate(pe.sections[0].get_file_offset()+12, struct.pack('<I', 0xfffff000), 'section bounds')
        pe.close()

if __name__ == '__main__':
    unittest.main()
