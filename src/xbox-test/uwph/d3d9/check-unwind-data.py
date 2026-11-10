#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""check-unwind-data.py FILE...

Play maps its own shims and donors into the launcher and runs them on the
guest's thread. When code inside such an image faults, the OS can only unwind
to that thread's __try/__except - and so to Play's crash record - through the
image's own .pdata. An image built with -fno-asynchronous-unwind-tables has an
empty exception directory, the mapper accepts that silently (there is nothing
to register), and a fault inside it is never dispatched: the launcher dies with
no record at all. That is exactly how a fault in the D3D9 shim's fixed-function
path killed the launcher with a probe log that simply stopped.

This checks the property Play depends on, and the same bounds Play's
prepareUnwind validates, so a build that would be silently undispatchable fails
here instead of on the console. Standard library only; no Wine, no console.
"""
import struct
import sys

ENTRY = 12          # RUNTIME_FUNCTION: BeginAddress, EndAddress, UnwindData
SCN_MEM_EXECUTE = 0x20000000
SCN_MEM_READ = 0x40000000


def sections(data, pe, optional_size, count):
    first = pe + 24 + optional_size
    out = []
    for i in range(count):
        name, vsize, vaddr, rawsize, rawptr = struct.unpack_from('<8sIIII', data, first + 40 * i)
        characteristics = struct.unpack_from('<I', data, first + 40 * i + 36)[0]
        out.append((name.rstrip(b'\0').decode(), vsize, vaddr, rawsize, rawptr, characteristics))
    return out


def check(path):
    data = open(path, 'rb').read()
    if data[:2] != b'MZ':
        raise ValueError(f'{path}: not a PE image')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError(f'{path}: not a PE image')
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    if struct.unpack_from('<H', data, pe + 24)[0] != 0x20b:
        raise ValueError(f'{path}: not a PE32+ image')
    secs = sections(data, pe, optional_size, count)
    rva, size = struct.unpack_from('<II', data, pe + 24 + 112 + 3 * 8)
    if not size:
        raise ValueError(f'{path}: no exception directory; a fault inside it cannot be dispatched '
                         '(build without -fno-asynchronous-unwind-tables)')
    if size % ENTRY:
        raise ValueError(f'{path}: exception directory size {size} is not a whole number of runtime functions')

    def readable(start, n):
        return any((c & SCN_MEM_READ) and start >= v and start - v + n <= max(vs, rs)
                   for _, vs, v, rs, _, c in secs)

    def executable(start, n):
        return any((c & SCN_MEM_EXECUTE) and start >= v and start - v + n <= max(vs, rs)
                   for _, vs, v, rs, _, c in secs)

    offset = None
    for _, vs, v, rs, rp, _ in secs:
        if v <= rva and rva + size <= v + max(vs, rs):
            offset = rp + (rva - v)
            break
    if offset is None:
        raise ValueError(f'{path}: exception directory is outside every section')
    if not readable(rva, size):
        raise ValueError(f'{path}: exception directory section is not readable')
    previous_end = 0
    for index in range(size // ENTRY):
        begin, end, unwind = struct.unpack_from('<III', data, offset + ENTRY * index)
        if begin >= end:
            raise ValueError(f'{path}: runtime function {index} is empty (0x{begin:x}..0x{end:x})')
        if begin < previous_end:
            raise ValueError(f'{path}: runtime function {index} is unsorted or overlaps its predecessor')
        if not executable(begin, end - begin):
            raise ValueError(f'{path}: runtime function {index} is not inside executable memory')
        if unwind % 4 or not readable(unwind, 4):
            raise ValueError(f'{path}: runtime function {index} has an unreadable unwind record')
        previous_end = end
    return size // ENTRY


def main(argv):
    status = 0
    for path in argv[1:]:
        try:
            entries = check(path)
        except (ValueError, struct.error) as error:
            print(f'FAIL {error}')
            status = 1
        else:
            print(f'PASS {path}: {entries} runtime functions, dispatchable')
    return status


if __name__ == '__main__':
    sys.exit(main(sys.argv))
