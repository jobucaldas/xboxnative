#!/usr/bin/env python3
"""Check direct (including delay-load) DLL imports of the four Megabonk PEs.

Routing is a plan, not proof that individual exports work on Xbox. Missing inputs
are reported; at least one target must exist. No transitive imports are scanned.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

TARGETS = ('Megabonk.exe', 'UnityPlayer.dll', 'GameAssembly.dll', 'baselib.dll')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--map', type=Path, default=Path(__file__).with_name('import_map.json'))
    parser.add_argument('--llvm-readobj', help='Override the automatically discovered llvm-readobj')
    args = parser.parse_args()
    try:
        mapping = json.loads(args.map.read_text())['dlls']
        for name, entry in mapping.items():
            if name != name.lower() or entry['class'] not in ('system', 'uwph', 'stub') or not entry['reason'].strip():
                raise ValueError(f'invalid map entry: {name}')
        candidates = ['llvm-readobj'] + [f'llvm-readobj-{v}' for v in range(30, 13, -1)]
        reader = args.llvm_readobj or next((p for c in candidates if (p := shutil.which(c))), None)
        if not reader:
            raise ValueError('llvm-readobj not found; install LLVM or pass --llvm-readobj')
        if not args.directory.is_dir():
            raise ValueError(f'not a directory: {args.directory}')
        seen = set()
        scanned = 0
        for target in TARGETS:
            path = args.directory / target
            if not path.is_file():
                print(f'MISSING: {target} (not scanned)')
                continue
            result = subprocess.run([reader, '--coff-imports', str(path)], text=True, capture_output=True, check=True)
            imports = {name.lower() for name in re.findall(r'^\s*Name:\s*(\S+)\s*$', result.stdout, re.MULTILINE)}
            if 'Format: COFF-x86-64' not in result.stdout:
                raise ValueError(f'{target}: expected an x64 COFF/PE binary')
            seen.update(imports)
            scanned += 1
            print(f'{target}: {len(imports)} imported DLLs')
            for name in sorted(imports - mapping.keys()):
                print(f'  UNCLASSIFIED: {name}')
        if not scanned:
            raise ValueError('none of the target binaries exist')
        unknown = seen - mapping.keys()
        print(f'{len(unknown)} unclassified DLLs ({len(seen)} unique DLLs, {scanned}/{len(TARGETS)} binaries scanned)')
        return 1 if unknown else 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        print(f'error: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
