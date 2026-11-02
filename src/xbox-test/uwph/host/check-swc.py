#!/usr/bin/env python3
"""Reject incomplete HWND-path evidence or writable executable mappings."""
import re
import sys
from pathlib import Path
s = Path(sys.argv[1]).read_text()
for marker in (
    'SWC mapped guest', 'SWC RegisterClassExW + CreateWindowExW OK',
    'SWC CreateSwapChainForHwnd hr=0x00000000',
    'SWC CreateSwapChain legacy hr=0x00000000',
    'SWC hwnd presented=300 hr=0x00000000',
    'SWC legacy presented=300 hr=0x00000000',
    'SWC RESULT entry=42 failures=0',
):
    assert marker in s, marker
assert s.count('SWC route HWND -> CoreWindow hr=0x00000000') == 2
assert re.search(r'SWC mapped pages=[1-9]\d* RWX=0', s)
assert re.search(r'WXL mapped LocalState PE relocations=[1-9]\d* imports=[1-9]\d* delta=0x[1-9a-f][0-9a-f]*', s)
lines = [x for x in s.splitlines() if 'WXL VirtualProtectFromApp' in x]
assert len(lines) >= 5
for line in lines:
    assert re.search(r'new=0x(?:1|2|4|20) ok=1 err=0$', line), line
assert not re.search(r'\bFAIL(?:ED)?\b|hr=0x(?!00000000)[0-9a-f]{8}', s)
print('SWC evidence PASS: HWND + legacy, 300 exact-S_OK Presents each, zero RWX')
