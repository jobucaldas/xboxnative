#!/usr/bin/env python3
"""Check fresh console probe against actual static imports, including counts."""
import re
from collections import Counter
import subprocess
import sys
from pathlib import Path
log = Path(sys.argv[1]).read_text()
assert 'MLP-PROBE-DONE' in log and 'MLP-PROBE-FAILED' not in log
assert 'awaiting fresh' not in log and 'WXL FAIL' not in log
assert not re.search(r'MLP STUB-CALL|ENTRY-STUB CALL|ENTRY-BUILTIN CALL',log)
assert 'MLP MISSING' not in log and 'unresolved' not in log.lower()
assert not re.search(r'probe-only|placeholder|generic',log,re.I)
assert log.count('WXL VirtualAllocFromApp initial=0x4 (RW)') == 3
assert log.count('entry NOT called') == 3
assert 'WXL RESULT entry=' not in log
assert not re.search(r'new=0x(?:40|80)\b', log)
protections = re.findall(r'VirtualProtectFromApp .* new=0x([0-9a-f]+) ok=(\d+)', log)
assert protections and all(ok == '1' and int(prot, 16) in (1, 2, 4, 0x20) for prot, ok in protections)
for image in ('Megabonk.exe', 'UnityPlayer.dll', 'baselib.dll'):
    dump = subprocess.check_output(['llvm-readobj-19', '--coff-imports', str(Path(sys.argv[2])/image)], text=True)
    imports = {}
    symbols = []
    dll = None
    for line in dump.splitlines():
        name = re.match(r'\s+Name: (\S+)', line)
        if name:
            dll = name[1].lower()
            assert dll not in imports
            imports[dll] = 0
        elif re.match(r'\s+Symbol:', line):
            imports[dll] += 1
            item = re.match(r'\s+Symbol:\s*(.*?)\s*\((\d+)\)',line)
            assert item, line
            symbols.append((dll,item[1] or '#'+item[2]))
    rows = re.findall(r'MLP SUMMARY image='+re.escape(image)+r' dll=(\S+) resolved=(\d+) stubbed=(\d+) first-missing=(\S+)', log)
    assert all(missing == 'none' for dll,resolved,stubbed,missing in rows), rows
    actual = {dll: int(resolved)+int(stubbed) for dll, resolved, stubbed, missing in rows}
    assert len(rows) == len(actual) and actual == imports, (image, actual, imports)
    block = log.split('MLP IMAGE '+image+'\n',1)[1].split('MLP IMAGE ',1)[0]
    routes = re.findall(r'MLP ROUTE ([^! ]+)!([^ ]+) kind=(system|uwph|stub) provider=(\S+) abi=named-x64',block)
    counts = {}
    for dll,symbol,kind,provider in routes:
        counts[dll] = counts.get(dll,0)+1
    assert Counter((dll,symbol) for dll,symbol,kind,provider in routes)==Counter(symbols), image
    assert counts == imports, (image,counts,imports)
    print(f'{image}: {len(rows)} DLL summaries and {sum(actual.values())} imports verified')
print('MLP console evidence PASS; no guest execution, zero RWX')
