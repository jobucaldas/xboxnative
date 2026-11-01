#!/usr/bin/env python3
"""Verify pinned PE donors and regenerate their complete import probe table."""
import argparse
import hashlib
import json
from pathlib import Path

import pefile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    rows = ['// Generated from pinned donor PEs; includes direct and delay imports.']
    for name, expected in json.loads((root / 'SHA256.json').read_text()).items():
        path = root / name
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise SystemExit(f'{name}: SHA256 mismatch')
        pe = pefile.PE(str(path))
        if pe.FILE_HEADER.Machine != 0x8664:
            raise SystemExit(f'{name}: expected x64')
        count = 0
        for key in ('DIRECTORY_ENTRY_IMPORT', 'DIRECTORY_ENTRY_DELAY_IMPORT'):
            for descriptor in getattr(pe, key, []):
                for symbol in descriptor.imports:
                    rows.append('{%s,%s,%s,%d},' % (
                        json.dumps(name), json.dumps(descriptor.dll.decode()),
                        json.dumps(symbol.name.decode()) if symbol.name else 'nullptr',
                        symbol.ordinal or 0))
                    count += 1
        print(f'{name}: hash verified, direct+delay imports={count}')
    generated = '\n'.join(rows) + '\n'
    if args.write:
        (root / 'imports.h').write_text(generated)
    elif generated != (root / 'imports.h').read_text():
        raise SystemExit('imports.h differs; run inspect.py --write')


if __name__ == '__main__':
    main()
