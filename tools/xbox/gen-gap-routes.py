#!/usr/bin/env python3
"""Generate the gap diagnostic's policy table; no change to mapper routing."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
mapping = json.loads((root / 'tools/xbox/import_map.json').read_text())['dlls']
output = '// Generated from tools/xbox/import_map.json by gen-gap-routes.py.\n'
for name, entry in sorted(mapping.items()):
    if name != name.lower() or entry['class'] not in ('system', 'uwph', 'stub', 'wine-pe'):
        raise ValueError(f'invalid route: {name}')
    output += '{%s, %s},\n' % (json.dumps(name), json.dumps(entry['class']))
(root / 'xbox-test/app/wine_gap_routes.h').write_text(output)
