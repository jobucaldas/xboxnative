#!/usr/bin/env python3
"""Validate real Play evidence; reject placeholders and partial behavioral runs."""
import json
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
text = (root / 'launch_probe.log').read_text()
result = json.loads((root / 'launch_result.json').read_text())
assert result['appid'] == 3405340 and result['ok'] is True
assert result['mapped'] == result['total'] == 3
assert result['gameCodeExecuted'] is False
assert 'MLP-PROBE-DONE' in text
for prefix, check_prefix, minimum in [('K32T SELFTEST', 'K32T CHECK', 40), ('K32T SEARCH-SELFTEST', 'K32T SEARCH-CHECK', 6), ('K32T PE-SELFTEST', 'K32T PE-CHECK', 13)]:
    rows = re.findall(re.escape(prefix) + r' checks=(\d+) failures=(\d+)', text)
    assert len(rows) == 1, (prefix, rows)
    assert int(rows[0][0]) >= minimum and int(rows[0][1]) == 0, (prefix, rows)
    checks = re.findall(re.escape(check_prefix) + r' ([^ ]+) pass=(\d+) error=\d+', text)
    assert len(checks) == int(rows[0][0]) and len({label for label, _ in checks}) == len(checks), (check_prefix, checks)
    assert all(passed == '1' for _, passed in checks)
assert not re.search(r'K32T (?:CHECK|SEARCH-CHECK|PE-CHECK) .* pass=0', text)
rows = re.findall(r'K32T SUMMARY kernel32 missing=(\d+) resolved=(\d+)', text)
assert len(rows) == 3 and all(int(missing) == 0 and int(resolved) > 0 for missing, resolved in rows), rows
assert not re.search(r'MLP (?:MISSING|STUB|STUB-CALL) kernel32\.dll!', text)
assert 'K32T ROUTE kernel32.dll!VirtualAlloc found=1' in text
assert 'K32T ROUTE kernel32.dll!VirtualProtect found=1' in text
assert not re.search(r'VirtualProtectFromApp.*ok=0|new=0x(?:40|80)\b', text)
for name in ['CreateToolhelp32Snapshot', 'Thread32First', 'Thread32Next', 'CreateWaitableTimerA', 'GetComputerNameW', 'GetStartupInfoA', 'GetSystemPowerStatus', 'SetDllDirectoryW', 'SetThreadAffinityMask', 'VerifyVersionInfoW']:
    assert f'K32T ROUTE kernel32.dll!{name} found=1' in text, name
assert any('SYNTHETIC' in line and 'press' in line for line in (root / 'boot.session.log').read_text().splitlines())
print(f'K32T PLAY PASS images=3 kernel32-missing=0 kernel32-imports={sum(int(n) for _, n in rows)} behavioral-checks>=59 no-game-code')
