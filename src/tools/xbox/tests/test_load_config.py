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


def pe(record, guard_cf=False, relocs=(), rdata_flags=0x40000040, cookie=lc.DEFAULT_COOKIE, delay=False,
       reloc_words=None):
    """Three sections at RVA 0x1000/0x2000/0x3000; .reloc grows for adversarial tables."""
    opt = bytearray(240)
    struct.pack_into('<HQ', opt, 0, 0x20B, 0)
    struct.pack_into('<Q', opt, 24, BASE)
    struct.pack_into('<II', opt, 32, 0x1000, 0x200)
    struct.pack_into('<I', opt, 56, 0x4000)
    struct.pack_into('<H', opt, 70, 0x4000 if guard_cf else 0)
    struct.pack_into('<I', opt, 108, 16)
    words = [(10 << 12) | off for off in relocs] if reloc_words is None else reloc_words
    block = struct.pack('<II', RDATA, 8 + 2 * len(words)) + b''.join(
        struct.pack('<H', word) for word in words)
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

    def test_highadj_addend_is_not_a_relocation(self):
        # The addend's high nibble is data, even when it resembles DIR64.
        for addend in (0xA058, 0, 0xFFFF):
            with self.subTest(addend=addend):
                image = pe(msvc_record(), reloc_words=(0x4200, addend))
                self.assertEqual(list(image.relocations()), [(RDATA + 0x200, 4)])
                text, blocked = self.report(image)
                self.assertTrue(blocked)
                self.assertIn('config field relocations=0', text)
        image = pe(msvc_record(), reloc_words=(0x4058, 0xA058, 0xA100, 0))
        self.assertEqual(list(image.relocations()), [(RDATA + 0x58, 4), (RDATA + 0x100, 10)])
        text, _ = self.report(image)
        self.assertIn('config field relocations=2: +0x058:SecurityCookie:type4 '
                      '+0x100:VolatileMetadataPointer:DIR64', text)
        self.assertNotIn('SecurityCookie:DIR64', text)
        image.directories[5] = (RELOC, image.directories[5][1] + 1)
        with self.assertRaisesRegex(lc.PEError, 'header truncated'):
            self.report(image)

    def test_highadj_requires_an_addend_in_its_own_block_even_after_sample_cap(self):
        images = [pe(msvc_record(), reloc_words=words)
                  for words in ((0x4058,), (0xA058,) * 17 + (0x4058,))]
        # A following block must not supply the preceding block's missing addend.
        data = bytearray(images[0].data)
        following = struct.pack('<IIH', RDATA, 10, 0xA100)
        data[0x60A:0x60A + len(following)] = following
        struct.pack_into('<II', data, 0x58 + 112 + 8 * 5, RELOC, 10 + len(following))
        images.append(lc.Image(bytes(data)))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'truncated-highadj.exe'
            for image in images:
                with self.subTest(size=image.directories[5][1]):
                    with self.assertRaisesRegex(lc.PEError, 'HIGHADJ.*truncated'):
                        self.report(image)
                    path.write_bytes(image.data)
                    with contextlib.redirect_stdout(io.StringIO()):
                        self.assertEqual(lc.main([str(path)]), 2)

    def test_section_overlap_half_open_unsorted_and_empty_semantics(self):
        original = pe(msvc_record()).data
        table = 0x58 + 240
        # Section order is not required to match virtual-address order.
        for ranges, overlaps in (
                (((0x1800, 0x200), (0x1000, 0x200), (0x1200, 0x200)), False),
                (((0x1800, 0x200), (0x1000, 0x200), (0x11FF, 0x200)), True),
                (((0x1000, 0x800), (0x1200, 0x100), (0x2000, 0x100)), True),
                (((0x1200, 0), (0x1000, 0x800), (0x1800, 0)), False),
                (((0x1000, 0x800), (0x1200, 0), (0x1800, 0)), False)):
            data = bytearray(original)
            for i, (va, extent) in enumerate(ranges):
                struct.pack_into('<IIII', data, table + 40 * i + 8, extent, va, 0, 0)
            with self.subTest(ranges=ranges):
                if overlaps:
                    with self.assertRaisesRegex(lc.PEError, 'overlapping virtual'):
                        lc.Image(bytes(data))
                else:
                    self.assertEqual([s['va'] for s in lc.Image(bytes(data)).sections],
                                     [va for va, _ in ranges])

    def test_section_validation_work_shape_and_maximum_fanout(self):
        comparisons = 0

        class CountedRVA(int):
            def __lt__(self, other):
                nonlocal comparisons
                comparisons += 1
                return int.__lt__(self, other)

            def __gt__(self, other):
                nonlocal comparisons
                comparisons += 1
                return int.__gt__(self, other)

        get = lc._get

        def counted_get(data, offset, fmt):
            values = get(data, offset, fmt)
            if fmt == '<8sIIII':
                values = (*values[:2], CountedRVA(values[2]), *values[3:])
            return values

        # Fail on a small discriminating case before attempting the maximum.
        # This tests work rather than timing; an all-pairs check exceeds the cap.
        for count in (256, 65535):
            table = 0x58 + 240
            data = bytearray(pe(b'').data[:table]) + bytearray(40 * count)
            struct.pack_into('<H', data, 0x46, count)
            struct.pack_into('<I', data, 0x58 + 56, RDATA + 16 * count)
            for i in range(count):
                struct.pack_into('<8sIIIIIIHHI', data, table + 40 * i,
                                 b'.virtual', 1, RDATA + 16 * (count - i - 1),
                                 0, 0, 0, 0, 0, 0, 0x40000040)
            comparisons = 0
            with mock.patch.object(lc, '_get', new=counted_get):
                image = lc.Image(bytes(data))
            self.assertEqual(len(image.sections), count)
            self.assertLess(comparisons, count * 16)

    def test_raw_ranges_are_read_only_views_of_input(self):
        image = pe(msvc_record())
        for rva, size in ((RDATA, 0x140), (DATA + 0x10, 8), image.directories[5]):
            with self.subTest(rva=rva, size=size):
                view = image.raw(rva, size)
                self.assertIsInstance(view, memoryview)
                self.assertIs(view.obj, image.data)
                self.assertTrue(view.readonly)
                section = image.section(rva, size)
                start = section['rptr'] + rva - section['va']
                self.assertEqual(view, image.data[start:start + size])

    def test_large_unknown_extension_is_counted_without_record_sized_copies(self):
        # Two MiB safely discriminates copies without allocating near MAX_FILE.
        size = 2 * 1024 * 1024
        data = bytearray(0x200 + size)
        data[:0x200] = pe(b'').data[:0x200]
        struct.pack_into('<H', data, 0x46, 1)
        struct.pack_into('<I', data, 0x58 + 56, RDATA + size)
        struct.pack_into('<II', data, 0x58 + 112 + 8 * 5, 0, 0)
        struct.pack_into('<II', data, 0x58 + 112 + 8 * 10, RDATA, size)
        struct.pack_into('<IIII', data, 0x58 + 240 + 8, size, RDATA, size, 0x200)
        struct.pack_into('<I', data, 0x200, size)
        for offset in (lc.KNOWN_END, size // 2, size - 1):
            data[0x200 + offset] = 0xFF
        image = lc.Image(bytes(data))
        tracemalloc.start()
        try:
            text, blocked = self.report(image)
            _, peak = tracemalloc.get_traced_memory()
        finally:
            tracemalloc.stop()
        self.assertTrue(blocked)
        self.assertIn('unknown extension +0x140..+0x200000: 3 nonzero bytes (not certified)', text)
        self.assertIn('VERDICT BLOCKED', text)
        self.assertLess(peak, 256 * 1024)

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

    def test_small_regular_file_never_requests_limit_sized_allocation(self):
        data = pe(msvc_record()).data
        fdopen = lc.os.fdopen
        requests = []

        @contextlib.contextmanager
        def bounded_fdopen(*args):
            with fdopen(*args) as stream:
                checked = mock.Mock(wraps=stream)

                def read(size):
                    requests.append(size)
                    # Check before calling BufferedReader.read: the old request
                    # must fail safely, without really allocating 512 MiB.
                    self.assertGreater(size, 0)
                    self.assertLessEqual(size, 64 * 1024)
                    return stream.read(size)

                checked.read.side_effect = read
                yield checked

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'small.exe'
            path.write_bytes(data)
            with mock.patch.object(lc.os, 'fdopen', side_effect=bounded_fdopen):
                tracemalloc.start()
                try:
                    image = lc.read_image(path)
                    _, peak = tracemalloc.get_traced_memory()
                finally:
                    tracemalloc.stop()
        self.assertTrue(requests)
        self.assertEqual(image.data, data)
        self.assertTrue(image.raw(RDATA, 4).readonly)
        self.assertLess(peak, 256 * 1024)

    def test_reader_does_not_retain_duplicate_whole_file_buffers(self):
        # Scale the limit, so even the old single read is safe to regression-test.
        data = pe(msvc_record()).data.ljust(2 * 1024 * 1024, b'\0')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'overlay.exe'
            path.write_bytes(data)
            with mock.patch.object(lc, 'MAX_FILE', 4 * 1024 * 1024):
                tracemalloc.start()
                try:
                    image = lc.read_image(path)
                    _, peak = tracemalloc.get_traced_memory()
                finally:
                    tracemalloc.stop()
        self.assertEqual(image.data, data)
        self.assertLess(peak, len(data) * 3 // 2)

    def test_chunked_reader_handles_short_reads_growth_and_exact_limit(self):
        for length in (64, 65):
            with self.subTest(length=length):
                source = io.BytesIO(b'x' * length)
                stream = mock.MagicMock()
                stream.__enter__.return_value = stream
                stream.fileno.return_value = 42
                requests = []

                def read(size):
                    requests.append(size)
                    self.assertGreater(size, 0)
                    self.assertLessEqual(size, min(16, 65 - source.tell()))
                    # A short read is not EOF. Continue until empty or over cap.
                    return source.read(min(size, 3))

                stream.read.side_effect = read
                info = mock.Mock(st_mode=0o100600, st_size=0)
                with mock.patch.object(lc, 'MAX_FILE', 64), \
                        mock.patch.object(lc, 'READ_CHUNK', 16, create=True), \
                        mock.patch.object(lc.os, 'open', return_value=42) as opened, \
                        mock.patch.object(lc.os, 'fdopen', return_value=stream), \
                        mock.patch.object(lc.os, 'fstat', return_value=info), \
                        mock.patch.object(lc, 'Image') as image:
                    if length > 64:
                        with self.assertRaisesRegex(lc.PEError, 'file too large'):
                            lc.read_image(Path('growing.exe'))
                        image.assert_not_called()
                    else:
                        lc.read_image(Path('exact.exe'))
                        image.assert_called_once_with(b'x' * length)
                opened.assert_called_once()
                stream.__exit__.assert_called_once()
                self.assertEqual(source.tell(), length)
                self.assertGreater(len(requests), 1)

    def test_untrusted_section_names_are_escaped_at_every_report_site(self):
        for name, escaped in ((b'\x1b[2J\x1b[H', r'\x1b[2J\x1b[H'),
                              (b'\nFORGED', r'\nFORGED'),
                              (b'\r\t\0\x7f', r'\r\t\x00\x7f'),
                              (b'\\n.txt', r'\\n.txt'),
                              (b'\x9b[2J', r'\ufffd[2J')):
            data = bytearray(pe(msvc_record()).data)
            for offset in (0x58 + 240, 0x58 + 240 + 40):
                data[offset:offset + 8] = name.ljust(8, b'\0')
            with self.subTest(name=name):
                text, blocked = self.report(lc.Image(bytes(data)))
                self.assertTrue(blocked)
                self.assertEqual(text.count(f'section={escaped}'), 4)
                self.assertTrue(all(c == '\n' or ' ' <= c <= '~' for c in text))
                self.assertEqual(len(text.splitlines()), 13)

    def test_untrusted_basenames_and_error_fields_are_escaped(self):
        name = 'input\x1b[2J\n\r\t\x7f\x9b\u2028\u2029\u202e\udcff\\n.exe'
        escaped = r'input\x1b[2J\n\r\t\x7f\x9b\u2028\u2029\u202e\udcff\\n.exe'
        error_text = 'bad\x1b[H\n\r\x9b\u202e\\n'
        for result, status in ((pe(msvc_record()), 1),
                               (OSError(5, error_text), 2), (lc.PEError(error_text), 2)):
            output = io.StringIO()
            with mock.patch.object(lc, 'read_image') as read, contextlib.redirect_stdout(output):
                if isinstance(result, Exception):
                    read.side_effect = result
                else:
                    read.return_value = result
                self.assertEqual(lc.main(['/private/synthetic-only/' + name]), status)
            text = output.getvalue()
            self.assertTrue(text.startswith(f'== {escaped}\n'))
            self.assertNotIn('/private/synthetic-only', text)
            self.assertTrue(all(c == '\n' or ' ' <= c <= '~' for c in text))
            if status == 2:
                self.assertEqual(text.splitlines()[1], r'ERROR bad\x1b[H\n\r\x9b\u202e\\n')
                self.assertEqual(len(text.splitlines()), 2)

    def test_argument_errors_escape_untrusted_options_and_program_name(self):
        output = io.StringIO()
        with mock.patch.object(sys, 'argv', ['/private/synthetic-only/diagnostic\x1b[H.py']), \
                contextlib.redirect_stderr(output), self.assertRaises(SystemExit) as raised:
            lc.main(['--bad\x1b[2J\nFORGED', 'synthetic.exe'])
        self.assertEqual(raised.exception.code, 2)
        text = output.getvalue()
        self.assertIn(r'diagnostic\x1b[H.py', text)
        self.assertIn(r'--bad\x1b[2J\nFORGED', text)
        self.assertNotIn('/private/synthetic-only', text)
        self.assertTrue(all(c == '\n' or ' ' <= c <= '~' for c in text))

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
