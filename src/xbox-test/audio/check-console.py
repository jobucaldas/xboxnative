#!/usr/bin/env python3
"""Require observed native sample progress and real-game import resolution."""
import csv
from pathlib import Path
import re
import sys

log = Path(sys.argv[1]).read_text()
imports = {(r['dll'], r['symbol']) for r in csv.DictReader(Path(sys.argv[2]).open())}
observed = {(dll, symbol): int(found) for dll, symbol, found in
            re.findall(r'^AUD ROUTE ([^!]+)!([^ ]+) native=([01])$', log, re.M)}
if len(imports) != 37 or any(observed.get(pair) != 1 for pair in imports):
    raise SystemExit('FAIL: real game audio/COM imports not all resolved natively')
match = re.search(r'^AUD RESULT hr=0x([0-9a-fA-F]+) SamplesPlayed=(\d+) nonzero=(\d+) queued=(\d+) pass=([01])$', log, re.M)
if not match:
    raise SystemExit('FAIL: no native tone result')
hr, played, nonzero, queued, passed = match.groups()
if int(hr, 16) != 0 or int(played) <= 0 or int(nonzero) <= 0 or int(queued) != 0 or passed != '1':
    raise SystemExit('FAIL: native tone did not complete with real sample progress')
if not re.search(r'^MLP SUMMARY image=UnityPlayer.dll dll=winmm.dll resolved=23 stubbed=0 first-missing=none$', log, re.M):
    raise SystemExit('FAIL: Unity WINMM IAT still has placeholders')
print(f'PASS: SamplesPlayed={played} nonzero={nonzero}; 37/37 native audio/COM imports; Unity WINMM missing=0')
