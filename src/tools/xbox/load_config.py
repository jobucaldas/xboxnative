#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Explain why the x64 mapper blocks a PE's load configuration (offline only).

Reads PE32+ files as untrusted bytes with Python's standard library. It never
loads, executes or modifies them. Every offset is checked against the file and
header-declared bounds. The report shows directory bounds, the security cookie,
CFG flags, nonzero fields, pointer slots and base relocations inside the record.

This is diagnosis, not acceptance. Play maps game images with
`LoadConfig::reject`, so any data directory 10 is BLOCKED with
`WXL FAIL unsupported runtime directory`. The tool never certifies unknown
records and never claims an image is runnable. Supporting a real MSVC record
means implementing what it asks for, such as cookie initialization, CFG and
volatile metadata. Ignoring or stripping the record is not support.
"""
import argparse
from pathlib import Path
import struct
import sys

MAX_FILE = 512 * 1024 * 1024
LOAD_CONFIG, DELAY_IMPORT, CLR = 10, 13, 14
GUARD_CF = 0x4000  # IMAGE_DLLCHARACTERISTICS_GUARD_CF
DEFAULT_COOKIE = 0x00002B992DDFA232  # MSVC x64 pre-initialization value
SCN_EXECUTE, SCN_WRITE = 0x20000000, 0x80000000
GATE = 'WXL FAIL unsupported runtime directory'

# IMAGE_LOAD_CONFIG_DIRECTORY64 through the 0x140 layout. 'va' marks absolute
# virtual addresses, which base relocations normally rewrite.
FIELDS = [
    (0x00, 4, 'Size', ''), (0x04, 4, 'TimeDateStamp', ''),
    (0x08, 2, 'MajorVersion', ''), (0x0A, 2, 'MinorVersion', ''),
    (0x0C, 4, 'GlobalFlagsClear', ''), (0x10, 4, 'GlobalFlagsSet', ''),
    (0x14, 4, 'CriticalSectionDefaultTimeout', ''),
    (0x18, 8, 'DeCommitFreeBlockThreshold', ''), (0x20, 8, 'DeCommitTotalFreeThreshold', ''),
    (0x28, 8, 'LockPrefixTable', 'va'), (0x30, 8, 'MaximumAllocationSize', ''),
    (0x38, 8, 'VirtualMemoryThreshold', ''), (0x40, 8, 'ProcessAffinityMask', ''),
    (0x48, 4, 'ProcessHeapFlags', ''), (0x4C, 2, 'CSDVersion', ''),
    (0x4E, 2, 'DependentLoadFlags', ''), (0x50, 8, 'EditList', 'va'),
    (0x58, 8, 'SecurityCookie', 'va'), (0x60, 8, 'SEHandlerTable', 'va'),
    (0x68, 8, 'SEHandlerCount', ''), (0x70, 8, 'GuardCFCheckFunctionPointer', 'va'),
    (0x78, 8, 'GuardCFDispatchFunctionPointer', 'va'), (0x80, 8, 'GuardCFFunctionTable', 'va'),
    (0x88, 8, 'GuardCFFunctionCount', ''), (0x90, 4, 'GuardFlags', ''),
    (0x94, 2, 'CodeIntegrity.Flags', ''), (0x96, 2, 'CodeIntegrity.Catalog', ''),
    (0x98, 4, 'CodeIntegrity.CatalogOffset', ''), (0x9C, 4, 'CodeIntegrity.Reserved', ''),
    (0xA0, 8, 'GuardAddressTakenIatEntryTable', 'va'), (0xA8, 8, 'GuardAddressTakenIatEntryCount', ''),
    (0xB0, 8, 'GuardLongJumpTargetTable', 'va'), (0xB8, 8, 'GuardLongJumpTargetCount', ''),
    (0xC0, 8, 'DynamicValueRelocTable', 'va'), (0xC8, 8, 'CHPEMetadataPointer', 'va'),
    (0xD0, 8, 'GuardRFFailureRoutine', 'va'), (0xD8, 8, 'GuardRFFailureRoutineFunctionPointer', 'va'),
    (0xE0, 4, 'DynamicValueRelocTableOffset', ''), (0xE4, 2, 'DynamicValueRelocTableSection', ''),
    (0xE6, 2, 'Reserved2', ''), (0xE8, 8, 'GuardRFVerifyStackPointerFunctionPointer', 'va'),
    (0xF0, 4, 'HotPatchTableOffset', ''), (0xF4, 4, 'Reserved3', ''),
    (0xF8, 8, 'EnclaveConfigurationPointer', 'va'), (0x100, 8, 'VolatileMetadataPointer', 'va'),
    (0x108, 8, 'GuardEHContinuationTable', 'va'), (0x110, 8, 'GuardEHContinuationCount', ''),
    (0x118, 8, 'GuardXFGCheckFunctionPointer', 'va'), (0x120, 8, 'GuardXFGDispatchFunctionPointer', 'va'),
    (0x128, 8, 'GuardXFGTableDispatchFunctionPointer', 'va'),
    (0x130, 8, 'CastGuardOsDeterminedFailureMode', 'va'), (0x138, 8, 'GuardMemcpyFunctionPointer', 'va'),
]
KNOWN_END = 0x140
GUARD_BITS = [
    (0x100, 'CF_INSTRUMENTED'), (0x200, 'CFW_INSTRUMENTED'), (0x400, 'CF_FUNCTION_TABLE_PRESENT'),
    (0x800, 'SECURITY_COOKIE_UNUSED'), (0x1000, 'PROTECT_DELAYLOAD_IAT'),
    (0x2000, 'DELAYLOAD_IAT_IN_ITS_OWN_SECTION'), (0x4000, 'CF_EXPORT_SUPPRESSION_INFO_PRESENT'),
    (0x8000, 'CF_ENABLE_EXPORT_SUPPRESSION'), (0x10000, 'CF_LONGJUMP_TABLE_PRESENT'),
    (0x20000, 'RF_INSTRUMENTED'), (0x40000, 'RF_ENABLE'), (0x80000, 'RF_STRICT'),
    (0x100000, 'RETPOLINE_PRESENT'), (0x400000, 'EH_CONTINUATION_TABLE_PRESENT'),
    (0x800000, 'XFG_ENABLED'), (0x1000000, 'CASTGUARD_PRESENT'), (0x2000000, 'MEMCPY_PRESENT'),
]


class PEError(ValueError):
    pass


def _get(data, offset, fmt):
    size = struct.calcsize(fmt)
    if offset < 0 or offset + size > len(data):
        raise PEError('truncated header')
    return struct.unpack_from(fmt, data, offset)


class Image:
    """Header-only PE32+ view with file-derived bounds; contents are never run."""

    def __init__(self, data):
        self.data = data
        if _get(data, 0, '<H')[0] != 0x5A4D:
            raise PEError('not a DOS/PE image')
        nt = _get(data, 0x3C, '<i')[0]
        if _get(data, nt, '<I')[0] != 0x4550:
            raise PEError('missing PE signature')
        machine, nsec, _, _, _, opt_size, self.characteristics = _get(data, nt + 4, '<HHIIIHH')
        opt = nt + 24
        if machine != 0x8664 or _get(data, opt, '<H')[0] != 0x20B:
            raise PEError('not an x64 PE32+ image')
        if opt_size < 112 or not nsec:
            raise PEError('bad optional header')
        self.image_base = _get(data, opt + 24, '<Q')[0]
        self.size_of_image = _get(data, opt + 56, '<I')[0]
        self.dll_characteristics = _get(data, opt + 70, '<H')[0]
        count = min(_get(data, opt + 108, '<I')[0], 16, (opt_size - 112) // 8)
        self.directories = [_get(data, opt + 112 + 8 * i, '<II') for i in range(count)]
        self.directories += [(0, 0)] * (16 - count)
        self.sections = []
        for i in range(nsec):
            raw = opt + opt_size + 40 * i
            name, vsize, va, rsize, rptr = _get(data, raw, '<8sIIII')
            flags = _get(data, raw + 36, '<I')[0]
            name = name.rstrip(b'\0').decode('ascii', 'replace') or '?'
            if rsize and rptr + rsize > len(data):
                raise PEError('section raw data beyond file')
            self.sections.append(dict(name=name, va=va, vsize=max(vsize, rsize),
                                      rsize=rsize, rptr=rptr, flags=flags))

    def section(self, rva, size=1):
        for s in self.sections:
            if s['va'] <= rva and rva + size <= s['va'] + s['vsize']:
                return s
        return None

    def raw(self, rva, size):
        """Return file bytes only when the whole range is raw-backed in one section."""
        s = self.section(rva, size)
        if s is None or rva + size > s['va'] + s['rsize']:
            return None
        start = s['rptr'] + rva - s['va']
        return self.data[start:start + size]

    def relocations(self):
        rva, size = self.directories[5]
        table = self.raw(rva, size) if size else b''
        if table is None:
            raise PEError('relocation directory not raw-backed')
        pos = 0
        while pos < len(table):
            if pos + 8 > len(table):
                raise PEError('relocation block header truncated')
            page, block = struct.unpack_from('<II', table, pos)
            if block < 8 or block % 2 or pos + block > len(table):
                raise PEError('relocation block length')
            for (word,) in struct.iter_unpack('<H', table[pos + 8:pos + block]):
                if word >> 12:
                    yield page + (word & 0xFFF), word >> 12
            pos += block


def rva_text(image, rva, size=1):
    s = image.section(rva, size)
    if s is None:
        return f'rva=0x{rva:x} OUTSIDE-SECTIONS'
    perms = ''.join(c for c, bit in (('R', 0x40000000), ('W', SCN_WRITE), ('X', SCN_EXECUTE))
                    if s['flags'] & bit)
    backed = 'raw' if rva + size <= s['va'] + s['rsize'] else 'virtual-only'
    return f'rva=0x{rva:x} section={s["name"]}({perms or "-"}) {backed}'


def diagnose(image):
    """Return (lines, blocked). Lines contain no paths or file contents beyond header fields."""
    out = []
    rva, size = image.directories[LOAD_CONFIG]
    for index, label in ((DELAY_IMPORT, 'DELAY_IMPORT'), (CLR, 'CLR')):
        d_rva, d_size = image.directories[index]
        if d_rva or d_size:
            out.append(f'dir={index} {label} rva=0x{d_rva:x} size=0x{d_size:x}: '
                       'generic gate applies only to unmapped loads; not the Play load-config block')
    guard_cf = bool(image.dll_characteristics & GUARD_CF)
    if not (rva or size):
        out.append(f'dir={LOAD_CONFIG} LOAD_CONFIG absent; DllCharacteristics GUARD_CF={"set" if guard_cf else "clear"}')
        return out, False
    out.append(f'dir={LOAD_CONFIG} LOAD_CONFIG BLOCKED: Play uses LoadConfig::reject -> "{GATE}"')
    out.append(f'directory rva=0x{rva:x} size=0x{size:x} end=0x{rva + size:x}')
    section = image.section(rva, size)
    if section is None:
        out.append('virtual bounds: directory not contained in one section (unsafe; fields not read)')
        return out, True
    out.append(f'virtual bounds: section={section["name"]} [0x{section["va"]:x},0x{section["va"] + section["vsize"]:x})')
    record = image.raw(rva, size)
    if record is None:
        out.append('raw bounds: directory not fully raw-backed (fields not read)')
        return out, True
    start = section['rptr'] + rva - section['va']
    out.append(f'raw bounds: file [0x{start:x},0x{start + size:x}) within section raw size 0x{section["rsize"]:x}; '
               f'section flags=0x{section["flags"]:08x}')
    declared = struct.unpack_from('<I', record)[0] if size >= 4 else None
    out.append(f'declared size={"unreadable" if declared is None else hex(declared)} '
               f'directory size=0x{size:x} known layout=0x{KNOWN_END:x}')
    limit = min(size, declared or 0)
    values = {}
    for off, width, name, kind in FIELDS:
        if off + width <= limit:
            values[name] = (off, kind, int.from_bytes(record[off:off + width], 'little'))
    inert = (size == KNOWN_END and declared == size and not any(record[4:]) and not guard_cf
             and not section['flags'] & (SCN_WRITE | SCN_EXECUTE))
    out.append(f'inert Wine record: {"yes (donor opt-in only)" if inert else "no"}')
    flags = values.get('GuardFlags', (0, '', 0))[2]
    names = [label for bit, label in GUARD_BITS if flags & bit]
    if flags >> 28:
        names.append(f'FUNCTION_TABLE_STRIDE={flags >> 28}')
    unknown = flags & ~(sum(b for b, _ in GUARD_BITS) | 0xF0000000)
    if unknown:
        names.append(f'UNKNOWN=0x{unknown:x}')
    shown = f'0x{flags:08x} [{" ".join(names) or "none"}]' if 'GuardFlags' in values else 'not in record'
    out.append(f'GuardFlags={shown} DllCharacteristics GUARD_CF={"set" if guard_cf else "clear"}')
    cookie = values.get('SecurityCookie', (0, '', 0))[2]
    if cookie:
        target = cookie - image.image_base
        raw = image.raw(target, 8) if 0 <= target < image.size_of_image else None
        state = ('unreadable' if raw is None else
                 'default (needs runtime initialization)' if int.from_bytes(raw, 'little') == DEFAULT_COOKIE
                 else 'non-default')
        where = rva_text(image, target, 8) if 0 <= target < image.size_of_image else 'outside image'
        out.append(f'security cookie: {where} value={state}')
    nonzero = [(off, name, kind, v) for name, (off, kind, v) in values.items() if v and name != 'Size']
    for off, name, kind, value in nonzero:
        if kind == 'va':
            target = value - image.image_base
            where = rva_text(image, target) if 0 <= target < image.size_of_image else 'outside image'
            out.append(f'unsupported +0x{off:03x} {name}: pointer slot -> {where}')
        else:
            out.append(f'unsupported +0x{off:03x} {name}=0x{value:x}')
    if limit > KNOWN_END:
        extra = sum(1 for b in record[KNOWN_END:limit] if b)
        out.append(f'unknown extension +0x{KNOWN_END:x}..+0x{limit:x}: {extra} nonzero bytes (not certified)')
    hits = []
    for target, kind in image.relocations():
        if rva <= target < rva + size:
            off = target - rva
            name = next((n for o, w, n, _ in FIELDS if o <= off < o + w), 'unknown')
            hits.append(f'+0x{off:03x}:{name}:{"DIR64" if kind == 10 else f"type{kind}"}')
    out.append(f'config field relocations={len(hits)}' + (': ' + ' '.join(hits) if hits else ''))
    out.append('VERDICT BLOCKED: unsupported runtime requirements remain; '
               'this report does not certify the record or claim the image runs')
    return out, True


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('pe', nargs='+', type=Path)
    args = parser.parse_args(argv)
    status = 0
    for path in args.pe:
        print(f'== {path.name}')
        try:
            if path.stat().st_size > MAX_FILE:
                raise PEError('file too large')
            lines, blocked = diagnose(Image(path.read_bytes()))
        except (OSError, PEError) as exc:
            print(f'ERROR {exc}')
            status = 2
            continue
        print('\n'.join(lines))
        status = max(status, int(blocked))
    return status


if __name__ == '__main__':
    sys.exit(main())
