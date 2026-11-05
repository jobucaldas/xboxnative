#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Emit the runtime resolver policy from lane imap's checked classification."""
import json
from pathlib import Path
import sys

# Import-map labels are data, not C++ identifiers. Keep every supported class
# explicit so new classifications cannot silently acquire native fallback.
ROUTE_KINDS = {"system": "system", "uwph": "uwph", "stub": "stub", "wine-pe": "wine_pe"}


def generate_routes(entries):
    # These indirect Wine imports belong to the same builtin boundary as user32.
    for name in ('ntdll.dll', 'win32u.dll'):
        if name not in entries:
            raise ValueError(f'missing Wine boundary in import map: {name}')
    rows = ['// Generated from src/tools/xbox/import_map.json; do not edit.',
            'static const Route routes[] = {']
    for name, value in sorted(entries.items()):
        classification = value['class']
        if classification not in ROUTE_KINDS:
            raise ValueError(f'unsupported route class for {name}: {classification}')
        rows.append(f'    {{L"{name}", RouteKind::{ROUTE_KINDS[classification]}}},')
    rows.append('};')
    return '\n'.join(rows) + '\n'


def main():
    source = Path(__file__).resolve().parents[4] / 'src/tools/xbox/import_map.json'
    entries = json.loads(source.read_text())['dlls']
    Path(sys.argv[1]).write_text(generate_routes(entries))


if __name__ == '__main__':
    main()
