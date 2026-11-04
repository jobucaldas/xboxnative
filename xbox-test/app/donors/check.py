#!/usr/bin/env python3
"""Require actual console donor execution and unchanged import-gap thresholds."""
import argparse
from collections import Counter
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('log', type=Path)
args = parser.parse_args()
lines = args.log.read_text().splitlines()
donors = ('hid.dll', 'setupapi.dll', 'version.dll', 'shcore.dll', 'avrt.dll')
for name in donors:
    expected = f'DWPE RUN {name} initialized=1 missing=0 behavior=1'
    if lines.count(expected) != 1:
        raise SystemExit(f'missing unique donor execution evidence: {expected}')
if any('DWPE FAIL' in line or 'MLP STUB-CALL' in line for line in lines):
    raise SystemExit('donor failure or probe placeholder executed')
# Count both STUB and MISSING so reclassifying a DLL cannot hide its gap.
historical = {'hid.dll', 'setupapi.dll', 'winmm.dll', 'gdi32.dll', 'shell32.dll',
              'opengl32.dll', 'imm32.dll', 'shlwapi.dll'}
counts = Counter()
for line in lines:
    match = re.match(r'MLP (?:STUB|MISSING) ([^!]+)!', line)
    if match and match[1] in historical:
        counts[match[1]] += 1
if sum(counts.values()) > 40:
    raise SystemExit(f'historical STUB surface still exceeds 40: {dict(counts)}')
for name, count in [('hid.dll', 14), ('setupapi.dll', 5), ('version.dll', 3)]:
    routed = [line for line in lines if line.startswith(f'DWPE ROUTE {name}!')]
    if len(routed) != count:
        raise SystemExit(f'{name}: expected {count} actual donor IAT routes, got {len(routed)}')
if 'MLP-PROBE-DONE no guest code executed; TLS/unwind deferred; zero RWX requested' not in lines:
    raise SystemExit('game mapping did not complete')
if not any(re.fullmatch(r'GAP: total=628 missing=\d+ win32u=120', line) for line in lines):
    raise SystemExit('complete Megabonk import oracle missing')
print(f'DWPE PASS donors=5 donor-IAT=22 historical-STUB-gap={sum(counts.values())} '
      f'hid/setupapi/winmm={sum(counts[n] for n in donors[:2] + ("winmm.dll",))}')
