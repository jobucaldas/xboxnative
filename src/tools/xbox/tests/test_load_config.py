# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic PE32+ fixtures for the offline load-config diagnosis."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import tracemalloc
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location('load_config', Path(__file__).parents[1] / 'load_config.py')
lc = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(lc)

BASE = 0x180000000
RDATA, DATA, RELOC = 0x1000, 0x2000, 0x3000


def pe(record, guard_cf=False, relocs=(), rdata_flags=0x40000040, cookie=lc.DEFAULT_COOKIE, delay=False):
    """Three sections at RVA 0x1000/0x2000/0x3000; .reloc grows for adversarial tables."""
    opt = bytearray(240)
    struct.pack_into('<HQ', opt, 0, 0x20B, 0)
    struct.pack_into('<Q', opt, 24, BASE)
    struct.pack_into('<II', opt, 32, 0x1000, 0x200)
    struct.pack_into('<I', opt, 56, 0x4000)
    struct.pack_into('<H', opt, 70, 0x4000 if guard_cf else 0)
    struct.pack_into('<I', opt, 108, 16)
    block = struct.pack('<II', RDATA, 8 + 2 * len(relocs)) + b''.join(
        struct.pack('<H', (10 << 12) | off) for off in relocs)
    reloc_size = max(0x200, (len(block) + 0x1FF) & ~0x1FF)
    struct.pack_into('<I', opt, 56, RELOC + ((reloc_size + 0xFFF) & ~0xFFF))
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
        extent = reloc_size if rva == RELOC else 0x200
        struct.pack_into('<8sIIIIIIHHI', head, 0x58 + len(opt) + 40 * i,
                         name, extent, rva, extent, 0x200 * (i + 1), 0, 0, 0, 0, flags)
    data = bytearray(0x200)
    struct.pack_into('<Q', data, 0x10, cookie)
    return lc.Image(bytes(head) + bytes(record).ljust(0x200, b'\0') + bytes(data) + block.ljust(reloc_size, b'\0'))


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
        self.assertIn('inert Wine file-byte candidate: no', text)
        self.assertTrue(text.endswith('does not certify the record or claim the image runs'))

    def test_inert_wine_record_still_blocked_on_play(self):
        record = bytearray(0x140)
        struct.pack_into('<I', record, 0, 0x140)
        text, blocked = self.report(pe(record))
        self.assertTrue(blocked)
        self.assertIn('inert Wine file-byte candidate: yes '
                      '(structural only; loader writes and mapper acceptance not checked)', text)
        self.assertIn('config field relocations=0', text)
        self.assertNotIn('unsupported +', text)
        # Same mutations the C++ inert check rejects.
        for image in (pe(record, guard_cf=True), pe(record, rdata_flags=0xC0000040),
                      pe(record + b'\0\0\0\0')):
            with self.subTest():
                self.assertIn('inert Wine file-byte candidate: no', self.report(image)[0])

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

    def test_inert_candidate_is_not_mapper_acceptance_even_with_relocations(self):
        record = bytearray(0x140)
        struct.pack_into('<I', record, 0, len(record))
        for targets in ((), (0x58,), (0x3C, 0x17C)):
            with self.subTest(targets=targets):
                image = pe(record, relocs=targets)
                if targets == (0x3C, 0x17C):
                    data = bytearray(image.data)
                    data[0x240:0x380] = data[0x200:0x340]
                    image = lc.Image(bytes(data))
                    image.directories[10] = (RDATA + 0x40, 0x140)
                text, blocked = self.report(image)
                self.assertTrue(blocked)
                self.assertIn('inert Wine file-byte candidate: yes', text)
                self.assertIn('structural only; loader writes and mapper acceptance not checked', text)
                self.assertNotIn('donor opt-in only', text)
                self.assertIn(f'config field relocations={len(targets)}', text)
                self.assertIn('VERDICT BLOCKED', text)

    def test_repeated_relocations_have_exact_counts_and_bounded_memory_and_output(self):
        image = pe(msvc_record(), relocs=(0x58,) * 100000 + (0x100,))
        self.assertLess(len(image.data), lc.MAX_FILE)
        tracemalloc.start()
        try:
            text, blocked = self.report(image)
            _, peak = tracemalloc.get_traced_memory()
        finally:
            tracemalloc.stop()
        self.assertTrue(blocked)
        self.assertIn('config field relocations=100001:', text)
        self.assertIn('(showing first 16; omitted 99985)', text)
        self.assertEqual(text.count(':SecurityCookie:DIR64'), 16)
        self.assertLess(len(text), 4096)
        self.assertLess(peak, 1024 * 1024)
        # A sample cap must not stop parsing: malformed trailing blocks still fail.
        image.directories[5] = (RELOC, image.directories[5][1] + 1)
        with self.assertRaisesRegex(lc.PEError, 'header truncated'):
            self.report(image)

    def test_guard_function_table_stride_includes_four_byte_rva(self):
        for extra in (1, 2, 15):
            with self.subTest(extra=extra):
                record = msvc_record()
                struct.pack_into('<I', record, 0x90, (extra << 28) | 0x400)
                text, blocked = self.report(pe(record))
                self.assertTrue(blocked)
                self.assertIn(f'CF_FUNCTION_TABLE_PRESENT FUNCTION_TABLE_STRIDE={4 + extra}]', text)

    def test_read_error_does_not_print_private_directories(self):
        output = io.StringIO()
        error = FileNotFoundError(2, 'No such file or directory', '/private/account/missing.exe')
        with mock.patch.object(lc.os, 'open', side_effect=error), contextlib.redirect_stdout(output):
            self.assertEqual(lc.main(['/private/account/missing.exe']), 2)
        self.assertIn('== missing.exe', output.getvalue())
        self.assertNotIn('/private/account', output.getvalue())

    def test_reader_uses_one_descriptor_and_caps_reads_despite_underreported_size(self):
        stream = mock.MagicMock()
        stream.__enter__.return_value = stream
        stream.fileno.return_value = 42
        stream.read.return_value = b'x' * 65
        info = mock.Mock(st_mode=0o100600, st_size=0)  # regular file, stale size
        with mock.patch.object(lc, 'MAX_FILE', 64), \
                mock.patch.object(lc.os, 'open', return_value=42) as opened, \
                mock.patch.object(lc.os, 'fdopen', return_value=stream), \
                mock.patch.object(lc.os, 'fstat', return_value=info) as inspected, \
                mock.patch.object(Path, 'stat', side_effect=AssertionError('path stat')), \
                mock.patch.object(Path, 'read_bytes', side_effect=AssertionError('unbounded read')):
            with self.assertRaisesRegex(lc.PEError, 'file too large'):
                lc.read_image(Path('growing.exe'))
        opened.assert_called_once()
        inspected.assert_called_once_with(42)
        stream.read.assert_called_once_with(65)
        stream.__exit__.assert_called_once()

    def test_regular_file_limit_and_cli_statuses(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'synthetic.exe'
            for record, status in ((b'', 0), (msvc_record(), 1), (struct.pack('<I', 4), 1)):
                data = pe(record).data
                path.write_bytes(data)
                with mock.patch.object(lc, 'MAX_FILE', len(data)), contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(lc.main([str(path)]), status)
                with mock.patch.object(lc, 'MAX_FILE', len(data) - 1), contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(lc.main([str(path)]), 2)

    @unittest.skipUnless(hasattr(os, 'mkfifo') and hasattr(os, 'O_NONBLOCK'), 'requires POSIX FIFO')
    def test_fifo_input_does_not_wait_for_writer(self):
        with tempfile.TemporaryDirectory() as directory:
            fifo = Path(directory) / 'input.fifo'
            os.mkfifo(fifo)
            result = subprocess.run([sys.executable, '-I', lc.__file__, str(fifo)],
                                    capture_output=True, text=True, timeout=3)
        self.assertEqual(result.returncode, 2)
        self.assertIn('input is not a regular file', result.stdout)

    @unittest.skipUnless(Path('/dev/zero').is_char_device(), 'requires /dev/zero')
    def test_device_and_device_symlink_are_rejected_before_read(self):
        with tempfile.TemporaryDirectory() as directory:
            link = Path(directory) / 'zero.exe'
            link.symlink_to('/dev/zero')
            for path in (Path('/dev/zero'), link, Path(directory)):
                with self.subTest(path=path):
                    result = subprocess.run([sys.executable, '-I', lc.__file__, str(path)],
                                            capture_output=True, text=True, timeout=3)
                    self.assertEqual(result.returncode, 2)
                    self.assertIn('ERROR', result.stdout)
                    self.assertNotIn(directory, result.stdout)

    def test_malformed_load_config_has_exit_two_not_unsupported_exit_one(self):
        original = pe(msvc_record()).data
        malformed = []
        directory_offset = 0x58 + 112 + 8 * 10
        for rva, size in ((0x3FF0, 0x140), (RDATA + 0x100, 0x140), (0, 0x140),
                          (RDATA, 0), (RDATA, 1), (RDATA, 2), (RDATA, 3)):
            data = bytearray(original)
            struct.pack_into('<II', data, directory_offset, rva, size)
            malformed.append(bytes(data))
        data = bytearray(original)
        struct.pack_into('<I', data, 0x58 + 240 + 16, 0x100)  # only partially raw-backed
        malformed.append(bytes(data))
        for declared in (0, 3, 0x141):
            data = bytearray(original)
            struct.pack_into('<I', data, 0x200, declared)
            malformed.append(bytes(data))
        with tempfile.TemporaryDirectory() as directory:
            bad = Path(directory) / 'bad.exe'
            good = Path(directory) / 'good.exe'
            good.write_bytes(original)
            for index, data in enumerate(malformed):
                bad.write_bytes(data)
                for paths in ((bad,), (bad, good), (good, bad)):
                    with self.subTest(case=index, paths=tuple(p.name for p in paths)):
                        output = io.StringIO()
                        with contextlib.redirect_stdout(output):
                            self.assertEqual(lc.main(list(map(str, paths))), 2)
                        self.assertIn('ERROR load-config', output.getvalue())

    def test_untrusted_bounds(self):
        image = pe(msvc_record())
        image.directories[10] = (RDATA + 0x100, 0x140)  # spans past .rdata
        with self.assertRaisesRegex(lc.PEError, 'not contained in one section'):
            self.report(image)
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
