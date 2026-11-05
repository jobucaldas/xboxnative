#!/usr/bin/env python3
"""Require fresh combined mapper/TLS evidence; do not accept marker alone."""
import re
import sys
from pathlib import Path
s = Path(sys.argv[1]).read_text()
assert not re.search(r'\bFAIL(?:ED)?\b|awaiting fresh', s)
assert s.count('WXL-TLS-OK: TLS value correct on two threads, callback ran, exception caught') == 1
assert 'WXL TLS RESULT failures=0' in s
assert 'LIFECYCLE RESULT failures=0' in s
assert s.count('WXL CXX mapped DLL destructor ran') == 2
assert s.count('WXL CXX mapped EXE caught 73') == 2
assert s.count('WXL TLS native template and callback correct on current thread') == 2
assert s.count('WXL TLS DllMain attach follows callback') == 2
assert 'LIFECYCLE unwind lookup ok' in s
entries = re.findall(r'WXL TLS native-index=(\d+) dynamic-key=(\d+) template=4 zero=4 callbacks=1', s)
assert len(entries) == 2 and entries[0][0] != entries[1][0]
assert entries[0][1] != entries[1][1]
assert s.count('WXL RtlAddFunctionTable ') == 3
assert s.count('WXL RtlDeleteFunctionTable ok=1') == 3
assert s.count('WXL DllMain DLL_PROCESS_ATTACH ok=1') == 2
assert s.count('WXL DllMain DLL_PROCESS_DETACH') == 2
lines = [line for line in s.splitlines() if 'WXL VirtualProtectFromApp' in line]
assert len(lines) == 29, len(lines)
for line in lines:
    m = re.search(r'rva=0x[0-9a-f]+ size=0x[0-9a-f]+ old=0x([0-9a-f]+) new=0x([0-9a-f]+) ok=1 err=0$', line)
    assert m and int(m[1], 16) in (1, 4) and int(m[2], 16) in (1, 2, 4, 0x20), line
assert s.count('WXL VirtualAllocFromApp initial=0x4 (RW)') == 4
print('WXL TLS evidence PASS: native TLS, two threads, callbacks, DLL lifecycle, cross-image C++ catch, 29 protections, zero RWX')
