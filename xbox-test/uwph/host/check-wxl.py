#!/usr/bin/env python3
"""Validate fresh console mapper evidence; reject missing or writable code protections."""
import re
import sys
from pathlib import Path
s = Path(sys.argv[1]).read_text()
assert re.search(r'WXL-OK \d+', s), 'missing guest output'
assert 'WXL RESULT entry=42 failures=0' in s
assert re.search(r'WXL mapped LocalState PE relocations=[1-9]\d* imports=3 delta=0x[1-9a-f][0-9a-f]*', s)
assert 'WXL VirtualAllocFromApp initial=0x4 (RW)' in s
lines = [line for line in s.splitlines() if 'WXL VirtualProtectFromApp' in line]
assert len(lines) == 6, f'expected six protection calls, got {len(lines)}'
protections = []
for line in lines:
    m = re.search(r'rva=0x([0-9a-f]+) size=0x([0-9a-f]+) old=0x([0-9a-f]+) new=0x([0-9a-f]+) ok=1 err=0$', line)
    assert m, f'incomplete/failed protection: {line}'
    protections.append(tuple(int(x, 16) for x in m.groups()))
assert protections == [(0,0x5000,4,1), (0,0x1000,1,2), (0x1000,0x1000,1,0x20), (0x2000,0x1000,1,2), (0x3000,0x1000,1,4), (0x4000,0x1000,1,2)], protections
assert not re.search(r'new=0x(?:40|80)(?:\s|$)', s), 'RWX requested'
assert not re.search(r'\bFAIL(?:ED)?\b', s), 'failure in console log'
print('WXL evidence PASS: entry=42, six logged protections, zero RWX')
