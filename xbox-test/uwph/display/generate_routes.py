#!/usr/bin/env python3
"""Emit the runtime resolver policy from lane imap's checked classification."""
import json
from pathlib import Path
import sys
source = Path(__file__).resolve().parents[3] / 'tools/xbox/import_map.json'
entries = json.loads(source.read_text())['dlls']
# These indirect Wine imports belong to the same builtin boundary as user32.
for name in ('ntdll.dll', 'win32u.dll'):
    if name not in entries:
        raise SystemExit(f'missing Wine boundary in import map: {name}')
rows = ['// Generated from tools/xbox/import_map.json; do not edit.',
        'static const Route routes[] = {']
for name, value in sorted(entries.items()):
    rows.append(f'    {{L"{name}", RouteKind::{value["class"]}}},')
rows.append('};')
Path(sys.argv[1]).write_text('\n'.join(rows) + '\n')
