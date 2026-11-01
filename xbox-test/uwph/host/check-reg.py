#!/usr/bin/env python3
"""Validate a Reg console log: every Reg* call on its own line and succeeding, REG-OK, zero RWX."""
import re
import sys
from pathlib import Path
s = Path(sys.argv[1]).read_text()
expect = sys.argv[2] if len(sys.argv) > 2 else None  # 'created' (fresh registry.json) or 'existing' (relaunch)
APIS = ['RegCreateKeyW', 'RegCreateKeyExW', 'RegSetValueExA', 'RegSetValueExW', 'RegQueryValueExA',
        'RegQueryValueExW', 'RegDeleteValueA', 'RegOpenKeyExW', 'RegCloseKey']
lines = [x.split('] ', 1)[-1] for x in s.splitlines()]
calls = [x for x in lines if re.match(r'REG Reg\w+ .* -> ', x)]
assert calls, 'no Reg* call lines'
for x in calls:
    assert re.search(r'-> (?:\d+ OK|data OK)\b', x), 'call failed: ' + x
for api in APIS:
    assert any(x.startswith('REG ' + api + ' ') for x in calls), 'never called: ' + api
    assert any(re.search(r'REG ROUTE advapi32\.dll!' + api + r' backend=(?:native|uwph)(?: provider=\S+)? found=1', x) for x in lines), 'unrouted: ' + api
ok = [x for x in lines if x.startswith('REG-OK calls=')]
assert len(ok) == 1 and ok[0].endswith('failures=0'), ok
assert int(re.search(r'calls=(\d+)', ok[0]).group(1)) == len(calls), 'REG-OK call count != call lines'
if expect:
    assert any(re.match(r'REG RegCreateKeyExW .* -> 0 OK ' + expect + r'$', x) for x in lines), 'expected disposition ' + expect
canary = [x for x in lines if x.startswith('REG NATIVE-CANARY ')]
assert len(canary) == 1, 'native canary line missing'
assert 'WXL RESULT entry=42 failures=0' in s
assert re.search(r'WXL mapped .*relocations=[1-9]\d* imports=[1-9]\d*', s) or re.search(r'relocations=[1-9]\d* imports=[1-9]\d*', s)
vp = [x for x in lines if 'WXL VirtualProtectFromApp' in x]
for x in vp:
    assert re.search(r'new=0x(?:1|2|4|20) ok=1 err=0$', x), x
assert not re.search(r'\bFAIL(?:ED)?\b|REG MISSING|WXL FAIL|found=0', s), 'failure marker'
backend = sorted(set(re.findall(r'backend=(\w+)', s)))
print('REG evidence PASS: %d Reg* call lines, 9/9 imports, REG-OK, backend=%s, zero RWX; %s' % (len(calls), ','.join(backend), 'native canary rc=' + canary[0].split('-> ',1)[1]))
