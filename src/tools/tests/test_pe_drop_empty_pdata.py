# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import os
import struct
import unittest

spec = importlib.util.spec_from_file_location('pdata', os.path.join(os.path.dirname(__file__), '..', 'pe-drop-empty-pdata.py'))
pdata = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pdata)


def image(entries):
    """A minimal PE32+ with one .pdata section holding `entries`."""
    optional = 112 + 16 * 8
    pe = 0x80
    raw = 0x200
    body = b''.join(struct.pack('<III', *e) for e in entries)
    data = bytearray(raw + max(len(body), 0x200))
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, pe)
    data[pe:pe + 4] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', data, pe + 4, 0x8664, 1, 0, 0, 0, optional, 0x2022)
    struct.pack_into('<H', data, pe + 24, 0x20b)
    struct.pack_into('<II', data, pe + 24 + 112 + 3 * 8, 0x1000, len(body))
    struct.pack_into('<8sIIIIIIHHI', data, pe + 24 + optional, b'.pdata', max(len(body), 0x200), 0x1000, max(len(body), 0x200), raw, 0, 0, 0, 0, 0x40000040)
    data[raw:raw + len(body)] = body
    return bytes(data), raw


def table(data, raw, count):
    return [struct.unpack_from('<III', data, raw + i * 12) for i in range(count)]


class DropEmptyPdata(unittest.TestCase):
    def test_drops_only_empty_entries_and_keeps_order(self):
        entries = [(0x1000, 0x1010, 0x2000), (0x1010, 0x1010, 0x2004), (0x1010, 0x1040, 0x2008), (0x1040, 0x1040, 0x200c), (0x1050, 0x1060, 0x2010)]
        data, raw = image(entries)
        out, dropped = pdata.normalize(data)
        self.assertEqual(dropped, 2)
        self.assertEqual(table(out, raw, 3), [entries[0], entries[2], entries[4]])
        self.assertEqual(out[raw + 36:raw + 60], bytes(24))                     # freed tail is zeroed
        self.assertEqual(struct.unpack_from('<I', out, 0x80 + 24 + 112 + 3 * 8 + 4)[0], 36)   # directory size shrank
        self.assertEqual(pdata.normalize(out)[1], 0)                            # idempotent

    def test_clean_table_is_unchanged(self):
        data, _ = image([(0x1000, 0x1010, 0x2000), (0x1010, 0x1020, 0x2004)])
        self.assertEqual(pdata.normalize(data), (data, 0))

    def test_still_fails_on_unsorted_overlapping_or_misaligned(self):
        for bad in ([(0x1020, 0x1030, 0x2000), (0x1000, 0x1010, 0x2004)],
                    [(0x1000, 0x1030, 0x2000), (0x1010, 0x1040, 0x2004)],
                    [(0x1000, 0x1010, 0x2001)]):
            data, _ = image(bad)
            with self.assertRaises(ValueError):
                pdata.normalize(data)

    def test_rejects_non_pe(self):
        with self.assertRaises(ValueError):
            pdata.normalize(b'not a pe')


if __name__ == '__main__':
    unittest.main()
