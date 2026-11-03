# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic PE32+ fixtures for the offline load-config diagnosis."""
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location('load_config', Path(__file__).parents[1] / 'load_config.py')
lc = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(lc)

BASE = 0x180000000
RDATA, DATA, RELOC = 0x1000, 0x2000, 0x3000


def pe(record, guard_cf=False, relocs=(), rdata_flags=0x40000040, cookie=lc.DEFAULT_COOKIE, delay=False):
    """Headers at 0x0, .rdata/.data/.reloc each 0x200 raw bytes at RVA 0x1000/0x2000/0x3000."""
    opt = bytearray(240)
    struct.pack_into('<HQ', opt, 0, 0x20B, 0)
    struct.pack_into('<Q', opt, 24, BASE)
    struct.pack_into('<II', opt, 32, 0x1000, 0x200)
    struct.pack_into('<I', opt, 56, 0x4000)
    struct.pack_into('<H', opt, 70, 0x4000 if guard_cf else 0)
    struct.pack_into('<I', opt, 108, 16)
    block = struct.pack('<II', RDATA, 8 + 2 * len(relocs)) + b''.join(
        struct.pack('<H', (10 << 12) | off) for off in relocs)
    struct.pack_into('<II', opt, 112 + 8 * 5, RELOC, len(block))
    if record:
        struct.pack_into('<II', opt, 112 + 8 * 10, RDATA, len(record))
    if delay:
        struct.pack_into('<II', opt, 112 + 8 * 13, DATA + 0x100, 0x40)
    head = bytearray(0x200)
    struct.pack_into('<HI', head, 0, 0x5A4D, 0)
    struct.pack_into('<i', head, 0x3C, 0x40)
    struct.pack_into('<IHHIIIHH', head, 0x40, 0x4550, 0x8664, 3, 0, 0, 0, len(opt), 0x2022)
    head[0x58:0x58 + len(opt)] = opt
    for i, (name, rva, flags) in enumerate(((b'.rdata', RDATA, rdata_flags),
                                            (b'.data', DATA, 0xC0000040),
                                            (b'.reloc', RELOC, 0x42000040))):
        struct.pack_into('<8sIIIIIIHHI', head, 0x58 + len(opt) + 40 * i,
                         name, 0x200, rva, 0x200, 0x200 * (i + 1), 0, 0, 0, 0, flags)
    data = bytearray(0x200)
    struct.pack_into('<Q', data, 0x10, cookie)
    return lc.Image(bytes(head) + bytes(record).ljust(0x200, b'\0') + bytes(data) + block.ljust(0x200, b'\0'))


def msvc_record():
    record = bytearray(0x140)
    struct.pack_into('<I', record, 0, 0x140)
    struct.pack_into('<Q', record, 0x58, BASE + DATA + 0x10)       # SecurityCookie
    struct.pack_into('<I', record, 0x90, 0x100)                    # CF_INSTRUMENTED
    struct.pack_into('<Q', record, 0x100, BASE + RDATA + 0x180)    # VolatileMetadataPointer
    return record


class LoadConfigDiagnosisTests(unittest.TestCase):
    def report(self, image):
        lines, blocked = lc.diagnose(image)
        return '\n'.join(lines), blocked

    def test_msvc_shaped_record_is_explained_and_blocked(self):
        text, blocked = self.report(pe(msvc_record(), relocs=(0x58, 0x100)))
        self.assertTrue(blocked)
        self.assertIn('dir=10 LOAD_CONFIG BLOCKED', text)
        self.assertIn(lc.GATE, text)
        self.assertIn('declared size=0x140 directory size=0x140', text)
        self.assertIn('raw bounds: file [0x200,0x340)', text)
        self.assertIn('security cookie: rva=0x2010 section=.data(RW) raw value=default', text)
        self.assertIn('GuardFlags=0x00000100 [CF_INSTRUMENTED] DllCharacteristics GUARD_CF=clear', text)
        self.assertIn('unsupported +0x100 VolatileMetadataPointer: VA target -> rva=0x1180 section=.rdata(R)', text)
        self.assertIn('unsupported +0x058 SecurityCookie: VA target -> rva=0x2010 section=.data(RW)', text)
        self.assertNotIn('pointer slot ->', text)
        self.assertIn('config field relocations=2: +0x058:SecurityCookie:DIR64 +0x100:VolatileMetadataPointer:DIR64', text)
        self.assertIn('inert Wine record: no', text)
        self.assertTrue(text.endswith('does not certify the record or claim the image runs'))

    def test_inert_wine_record_still_blocked_on_play(self):
        record = bytearray(0x140)
        struct.pack_into('<I', record, 0, 0x140)
        text, blocked = self.report(pe(record))
        self.assertTrue(blocked)
        self.assertIn('inert Wine record: yes (donor opt-in only)', text)
        self.assertIn('config field relocations=0', text)
        self.assertNotIn('unsupported +', text)
        # Same mutations the C++ inert check rejects.
        for image in (pe(record, guard_cf=True), pe(record, rdata_flags=0xC0000040),
                      pe(record + b'\0\0\0\0')):
            with self.subTest():
                self.assertIn('inert Wine record: no', self.report(image)[0])

    def test_non_default_cookie_and_unknown_extension(self):
        record = msvc_record() + bytes([0, 1, 0, 0, 0, 0, 0, 0])
        struct.pack_into('<I', record, 0, len(record))
        text, _ = self.report(pe(record, cookie=0x1234))
        self.assertIn('value=non-default', text)
        short = bytearray(0x40)
        struct.pack_into('<I', short, 0, 0x40)
        self.assertIn('GuardFlags=not in record', self.report(pe(short))[0])
        self.assertIn('unknown extension +0x140..+0x148: 1 nonzero bytes (not certified)', text)

    def test_absent_config_and_delay_directory_are_distinguished(self):
        text, blocked = self.report(pe(b''))
        self.assertFalse(blocked)
        self.assertIn('dir=10 LOAD_CONFIG absent', text)
        text, _ = self.report(pe(msvc_record(), delay=True))
        self.assertIn('dir=13 DELAY_IMPORT rva=0x2100 size=0x40: generic gate applies only to unmapped loads', text)

    def test_declared_directory_table_cannot_be_silently_truncated(self):
        data = bytearray(pe(msvc_record()).data)
        struct.pack_into('<H', data, 0x40 + 20, 112)
        with self.assertRaisesRegex(lc.PEError, 'data directories exceed'):
            lc.Image(bytes(data))

    def test_virtual_sections_must_fit_and_not_alias(self):
        original = pe(msvc_record()).data
        data = bytearray(original)
        struct.pack_into('<I', data, 0x58 + 56, 0x1100)
        with self.assertRaisesRegex(lc.PEError, 'SizeOfImage'):
            lc.Image(bytes(data))
        data = bytearray(original)
        struct.pack_into('<I', data, 0x58 + 240 + 40 + 12, RDATA)
        with self.assertRaisesRegex(lc.PEError, 'overlapping virtual'):
            lc.Image(bytes(data))

    def test_dir64_straddling_record_edges_is_reported(self):
        image = pe(msvc_record(), relocs=(0x3C, 0x17C))
        data = bytearray(image.data)
        data[0x240:0x380] = data[0x200:0x340]
        image = lc.Image(bytes(data))
        image.directories[10] = (RDATA + 0x40, 0x140)
        text, blocked = self.report(image)
        self.assertTrue(blocked)
        self.assertIn('config field relocations=2:', text)
        self.assertIn('-0x004:unknown:DIR64:straddles-start', text)
        self.assertIn('+0x13c:GuardMemcpyFunctionPointer:DIR64:straddles-end', text)

    def test_read_error_does_not_print_private_directories(self):
        output = io.StringIO()
        error = FileNotFoundError(2, 'No such file or directory', '/private/account/missing.exe')
        with mock.patch.object(Path, 'stat', side_effect=error), contextlib.redirect_stdout(output):
            self.assertEqual(lc.main(['/private/account/missing.exe']), 2)
        self.assertIn('== missing.exe', output.getvalue())
        self.assertNotIn('/private/account', output.getvalue())

    def test_untrusted_bounds(self):
        image = pe(msvc_record())
        image.directories[10] = (RDATA + 0x100, 0x140)  # spans past .rdata
        self.assertIn('not contained in one section', self.report(image)[0])
        data = bytearray(image.data)
        for cut in (0x3E, 0x100, 0x500):
            with self.subTest(cut=cut), self.assertRaises(lc.PEError):
                lc.Image(bytes(data[:cut]))
        image = pe(msvc_record())
        image.directories[5] = (RELOC, 0x7)
        with self.assertRaises(lc.PEError):
            self.report(image)


if __name__ == '__main__':
    unittest.main()
