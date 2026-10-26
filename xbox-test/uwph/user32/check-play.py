#!/usr/bin/env python3
"""Validate fresh real-Play evidence; never accept a placeholder count or banner alone."""
import pathlib
import re
import sys
path = pathlib.Path(sys.argv[1])
probe = (path / 'launch_probe.log').read_text()
boot = (path / 'boot.session.log').read_text()
assert 'U32C CHECK FAIL' not in probe and 'U32C FAIL' not in probe
assert 'U32C TEST FAILED' not in probe
assert 'U32C TEST PASS class/long/property/geometry/lifecycle' in probe
assert 'U32C RegisterClassExW/CreateWindowExW/GetWindowLongPtrW roundtrip OK' in probe
assert 'U32C own mapped test entry=42; game entries remain uncalled' in probe
rows = re.findall(r'^U32C user32 (\w+) (implemented|missing)$', probe, re.M)
assert len(rows) == 121 and len(dict(rows)) == 121
missing = sum(state == 'missing' for _, state in rows)
assert missing <= 91
assert dict(rows)['RegisterDeviceNotificationW'] == 'missing'
assert dict(rows)['UnregisterDeviceNotification'] == 'missing'
assert f'U32C user32 total=121 implemented={121-missing} missing={missing}' in probe
assert 'MLP-PROBE-DONE' in probe
assert 'launch: RESULT ok=1 mapped=3/3:' in boot
# The own test is mapped before the three game images, with successful FromApp
# protections. Game entry invocation is explicitly excluded by the launcher.
assert len(re.findall(r'^WXL mapped LocalState PE ', probe, re.M)) == 4
protections = re.findall(r'^WXL VirtualProtectFromApp .*$', probe, re.M)
assert protections and all('ok=1' in p and 'new=0x40' not in p for p in protections)
assert 'gameCodeExecuted":false' in (path / 'launch_result.json').read_text()
print(f'U32C PLAY PASS complete user32=121 implemented={121-missing} missing={missing}; own PE entry=42; game not executed')
