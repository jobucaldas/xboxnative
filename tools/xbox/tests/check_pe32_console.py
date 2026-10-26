#!/usr/bin/env python3
"""Compare real Play's PE32 import list with independent offline Wine CSVs."""
import csv
import json
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[3]
evidence = Path(sys.argv[1]) if len(sys.argv) == 2 else Path('/workspace/x64-run/runs/pe32')
results = []
for app, game in [(480, 'spacewar'), (48000, 'limbo')]:
    run = evidence/f'play-{app}'
    expected_pairs = {tuple(line.split(',', 1)) for line in
                      (root/f'runs/pe32/{game}-exe-imports.txt').read_text().splitlines()}
    expected = {(r['dll'], r['symbol']): r for r in
                csv.DictReader((root/f'runs/pe32/{game}-i386.csv').open())}
    rows = list(csv.DictReader((run/f'pe32-imports-{app}.csv').open()))
    assert len(rows) == len(expected_pairs)
    assert {(r['dll'], r['symbol']) for r in rows} == expected_pairs
    for row in rows:
        offline = expected[(row['dll'], row['symbol'])]
        assert row['machine'] == 'i386'
        assert row['console_route'] == offline['console_route'], (row, offline)
        assert row['win32u'] == str(int(int(offline['n_win32u']) > 0)), (row, offline)
    gap = sum(r['console_route'] not in ('system', 'game') for r in rows)
    probe = (run/'launch_probe.log').read_text()
    match = re.search(r'^PE32 mapped, imports=(\d+), gap=(\d+); relocations=(\d+) reloc-errors=0;', probe, re.M)
    assert match and tuple(map(int, match.groups()[:2])) == (len(rows), gap)
    result = json.loads((run/'launch_result.json').read_text())
    assert result['gameCodeExecuted'] is False
    assert result['ok'] is True and result['mapped'] == result['total'] == 1
    boot = run/'boot.session.log'
    if not boot.is_file():
        boot = run.parent/f'play-{app}.txt'  # preserved play-test stdout
    assert 'launch: RESULT ok=1 mapped=1/1: PE32 mapped' in boot.read_text()
    results.append(dict(appid=app, imports=len(rows), gap=gap, relocations=int(match[3]),
                        relocation_errors=0, game_code_executed=False))
print(json.dumps(results, indent=2))
