#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""pe-drop-empty-pdata.py FILE...

GNU ld can emit zero-length x64 runtime-function entries (BeginAddress == EndAddress) for the empty cold/merged
pieces of a function. The OS function-table API rejects them and Play's mapper validates every entry before it
registers unwind data, so a linked DLL carrying them is never initialized. This removes exactly those entries from
the exception directory of a freshly linked build output (our own Mesa build, never a game or vendor file): the
remaining entries keep their order, the directory size shrinks and the freed tail of the section is zeroed.
It fails if, after that, any entry is still empty, unsorted, or overlaps its predecessor. Standard library only.
"""
import struct
import sys

ENTRY = struct.Struct('<III')


def normalize(data: bytes):
    """Return (new image bytes, number of entries dropped); raise ValueError on a malformed table."""
    image = bytearray(data)
    if image[:2] != b'MZ':
        raise ValueError('not a PE image')
    pe = struct.unpack_from('<I', image, 0x3c)[0]
    if image[pe:pe + 4] != b'PE\0\0' or struct.unpack_from('<H', image, pe + 24)[0] != 0x20b:
        raise ValueError('not a PE32+ image')
    sections_count = struct.unpack_from('<H', image, pe + 6)[0]
    optional_size = struct.unpack_from('<H', image, pe + 20)[0]
    directory = pe + 24 + 112 + 3 * 8          # DataDirectory[3] = exception table
    rva, size = struct.unpack_from('<II', image, directory)
    if not size:
        return bytes(image), 0
    first_section = pe + 24 + optional_size
    for index in range(sections_count):
        name, vsize, vaddr, rawsize, rawptr = struct.unpack_from('<8sIIII', image, first_section + 40 * index)
        if vaddr <= rva and rva + size <= vaddr + max(vsize, rawsize):
            offset = rawptr + (rva - vaddr)
            break
    else:
        raise ValueError('exception directory is outside every section')
    count = size // ENTRY.size
    entries = [ENTRY.unpack_from(image, offset + i * ENTRY.size) for i in range(count)]
    kept = [e for e in entries if e[0] != e[1]]
    previous_end = 0
    for begin, end, unwind in kept:
        if begin > end or begin < previous_end or unwind % 4:
            raise ValueError('runtime function table is unsorted, overlapping or misaligned after dropping empty entries')
        previous_end = end
    dropped = count - len(kept)
    if dropped:
        for i, entry in enumerate(kept):
            ENTRY.pack_into(image, offset + i * ENTRY.size, *entry)
        image[offset + len(kept) * ENTRY.size:offset + count * ENTRY.size] = bytes(dropped * ENTRY.size)
        struct.pack_into('<I', image, directory + 4, len(kept) * ENTRY.size)
    return bytes(image), dropped


def main(paths):
    for path in paths:
        with open(path, 'rb') as handle:
            original = handle.read()
        image, dropped = normalize(original)
        if dropped:
            with open(path, 'wb') as handle:
                handle.write(image)
        print('%s: dropped %d empty runtime function entries' % (path, dropped))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
