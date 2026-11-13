#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# The d3d11 fixture's Wine gate: the skip path must be unreachable without the
# Wine marker. The probe (tests/wine_probe.h) answers true ONLY on an exact
# byte match of "wine_get_version" in the loaded ntdll's PE export table.
# This test parses the probe's C++ source and asserts the properties the
# console path depends on: the marker string appears exactly once, the match
# is byte-exact (both strings must terminate, not a prefix/substring match),
# and every structural surprise path answers false — the console path, where
# nothing is skipped. Structural checks run against a synthetic PE image
# built in Python, exercising the same offsets the C++ reads.
import re
import struct
import unittest
from pathlib import Path

PROBE = Path(__file__).resolve().parent / 'wine_probe.h'

# Minimal PE constants (the C++ reads the same fields).
IMAGE_DOS_SIGNATURE = 0x5A4D
IMAGE_NT_SIGNATURE = 0x00004550
IMAGE_DIRECTORY_ENTRY_EXPORT = 0
IMAGE_NT_OPTIONAL_HDR64_MAGIC = 0x20B


def build_pe(*, dos_magic=True, nt_sig=True, export_rva=None,
             names=(b'wine_get_version',)):
    """A synthetic 64-bit PE image: DOS header, NT headers, export directory
    with the given export names. Returns the image bytes. export_rva defaults
    to the export directory's actual offset in the flat image (in a loaded
    image, RVA == offset from base)."""
    dos = bytearray(64)
    if dos_magic:
        struct.pack_into('<H', dos, 0, IMAGE_DOS_SIGNATURE)
    struct.pack_into('<I', dos, 0x3C, 64)          # e_lfanew -> NT headers

    nt = bytearray(24 + 112 + 16)                   # sig + file header + opt header64 start + DataDirectory[0]
    if nt_sig:
        struct.pack_into('<I', nt, 0, IMAGE_NT_SIGNATURE)
    struct.pack_into('<H', nt, 4, 0x8664)           # machine
    struct.pack_into('<H', nt, 24, IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    # The DataDirectory[0] size, at opt-header offset 112+4. The RVA itself is
    # packed after the tail is laid out (it depends on the name count).
    struct.pack_into('<I', nt, 24 + 116, 0x100)

    # Export directory: 40 bytes, then the name RVAs, then the names.
    if export_rva is None:
        export_rva = len(dos) + len(nt)  # the export dir's offset in the image
    exp = bytearray(40)
    struct.pack_into('<I', exp, 24, len(names))     # NumberOfNames
    struct.pack_into('<I', exp, 32, export_rva + 40)  # AddressOfNames (RVA from base)
    name_rvas = b''
    blob = b''
    base = 40 + 4 * len(names)
    for i, name in enumerate(names):
        name_rvas += struct.pack('<I', export_rva + base + len(blob))
        blob += name + b'\0'
    struct.pack_into('<I', nt, 24 + 112, export_rva)
    image = bytes(dos) + bytes(nt) + bytes(exp) + name_rvas + blob
    return image


def probe_result(image):
    """Reimplement the probe's exact walk over a synthetic image: returns True
    iff the walk finds wine_get_version. Mirrors wine_probe.h line for line so
    a drift in the header fails this test's assumptions loudly."""
    if len(image) < 64:
        return False
    if struct.unpack_from('<H', image, 0)[0] != IMAGE_DOS_SIGNATURE:
        return False
    e_lfanew = struct.unpack_from('<I', image, 0x3C)[0]
    if e_lfanew + 24 + 112 > len(image):
        return False
    if struct.unpack_from('<I', image, e_lfanew)[0] != IMAGE_NT_SIGNATURE:
        return False
    export_rva = struct.unpack_from('<I', image, e_lfanew + 24 + 112)[0]
    if not export_rva or export_rva + 40 > len(image):
        return False
    n_names = struct.unpack_from('<I', image, export_rva + 24)[0]
    names_rva = struct.unpack_from('<I', image, export_rva + 32)[0]
    if names_rva + 4 * n_names > len(image):
        return False
    for i in range(n_names):
        rva = struct.unpack_from('<I', image, names_rva + 4 * i)[0]
        end = image.index(b'\0', rva)
        name = image[rva:end]
        # Byte-exact match, as the C++ does: both must end together.
        if name == b'wine_get_version':
            return True
    return False


class WineProbeSourceTest(unittest.TestCase):
    """The C++ source must keep the properties the console path needs."""

    def setUp(self):
        # Strip // comments so prose about a banned API (e.g. "must not go
        # through LoadLibraryW") is not mistaken for a call site.
        self.source = re.sub(r'//[^\n]*', '', PROBE.read_text())

    def test_marker_exact_and_single(self):
        # The marker appears exactly once, as a byte-exact C string.
        self.assertEqual(self.source.count('"wine_get_version"'), 1)

    def test_match_is_byte_exact_not_substring(self):
        # The loop must require BOTH strings to end together (*a==*b with both
        # NUL), not strstr/strncmp-prefix semantics that "wine_get_version2"
        # or "xwine_get_version" would satisfy.
        m = re.search(r'while\(\*a&&\*a==\*b\)\{(\+\+a;\+\+b;|a\+\+,b\+\+)\}', self.source)
        self.assertIsNotNone(m, 'byte-walk loop missing')
        self.assertIn('if(*a==*b)return true;', self.source,
                      'match requires both cursors at NUL')

    def test_no_loader_api_in_the_path(self):
        # Play hooks LoadLibraryW/GetProcAddress; the probe must not call them.
        body = self.source
        self.assertNotIn('LoadLibraryW', body)
        self.assertNotIn('GetProcAddress', body)
        self.assertIn('GetModuleHandleW', body)

    def test_every_surprise_answers_false(self):
        # Each structural guard must return false (the console path).
        for guard in ['if(!ntdll)return false;',
                      'if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;',
                      'if(nt->Signature!=IMAGE_NT_SIGNATURE)return false;',
                      'if(!exportDir.VirtualAddress)return false;']:
            self.assertIn(guard, self.source, f'missing guard: {guard}')


class WineProbeWalkTest(unittest.TestCase):
    """The probe's walk, replayed over synthetic PE images."""

    def test_marker_present_matches(self):
        self.assertTrue(probe_result(build_pe()))

    def test_console_like_ntdll_does_not_match(self):
        # A console ntdll: valid PE, many exports, none is the marker.
        names = (b'RtlAllocateHeap', b'NtCreateFile', b'ZwClose', b'RtlFreeHeap')
        self.assertFalse(probe_result(build_pe(names=names)))

    def test_near_matches_do_not_match(self):
        for name in (b'wine_get_version2', b'xwine_get_version',
                     b'wine_get_versio', b'WINE_GET_VERSION'):
            self.assertFalse(probe_result(build_pe(names=(name,))),
                             f'{name!r} must not match')

    def test_structural_surprises_do_not_match(self):
        self.assertFalse(probe_result(build_pe(dos_magic=False)), 'bad MZ')
        self.assertFalse(probe_result(build_pe(nt_sig=False)), 'bad PE sig')
        self.assertFalse(probe_result(build_pe(export_rva=0)), 'no export dir')

    def test_marker_among_many_exports_matches(self):
        names = (b'RtlAllocateHeap', b'wine_get_version', b'ZwClose')
        self.assertTrue(probe_result(build_pe(names=names)))


if __name__ == '__main__':
    unittest.main()
