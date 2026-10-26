#!/usr/bin/env python3
"""Independently cross-check complete direct+delay imports with LLVM and pefile."""
import csv
import pathlib
import re
import subprocess
import sys
import pefile
root = pathlib.Path(sys.argv[1])
pairs = set()
llvm_pairs = set()
for path in sorted(root.iterdir()):
    if path.suffix.lower() not in ('.exe', '.dll'):
        continue
    pe = pefile.PE(str(path), fast_load=True)
    pe.parse_data_directories(directories=[1, 13])
    for kind in ('DIRECTORY_ENTRY_IMPORT', 'DIRECTORY_ENTRY_DELAY_IMPORT'):
        for desc in getattr(pe, kind, []):
            for imp in desc.imports:
                pairs.add((desc.dll.decode().lower(), imp.name.decode() if imp.name else '#' + str(imp.ordinal)))
    # --coff-imports prints Import and DelayImport blocks in LLVM 19.
    text = subprocess.check_output(['llvm-readobj-19', '--coff-imports', str(path)], text=True)
    dll = None
    for line in text.splitlines():
        name = re.match(r'  Name: (.+)', line)
        symbol = re.match(r'\s+Symbol: (.*) \((\d+)\)$', line)
        if name:
            dll = name[1].lower()
        elif symbol:
            # pefile names known ws2_32/oleaut32 ordinal imports automatically.
            # Normalize LLVM's raw ordinals using that public ordinal-name table;
            # both parsers still read import records independently.
            name = symbol[1]
            if not name:
                known = pefile.ordlookup.ordLookup(dll.encode(), int(symbol[2]))
                name = known.decode() if known else '#' + symbol[2]
            llvm_pairs.add((dll, name))
assert pairs == llvm_pairs, (sorted(pairs - llvm_pairs), sorted(llvm_pairs - pairs))
expected = {(r['dll'], r['symbol']) for r in csv.DictReader(open(pathlib.Path(__file__).with_name('megabonk-real-imports.csv')))}
assert pairs == expected, 'committed import oracle differs'
print(f'U32C independent PE audit PASS pairs={len(pairs)} user32={sum(d == "user32.dll" for d, _ in pairs)}')
