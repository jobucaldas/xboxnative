#!/usr/bin/env python3
"""Embed the checked-in DXBC without requiring a shader compiler at build time."""
from pathlib import Path
p = Path(__file__).resolve().parent
s = '// Generated from triangle_*.dxbc; regenerate with embed.py.\n'
for stage in ('vs', 'ps'):
    b = (p / f'triangle_{stage}.dxbc').read_bytes()
    s += f'static const unsigned char triangle_{stage}[] = {{\n'
    s += ''.join(' ' + ','.join(f'0x{x:02x}' for x in b[i:i+16]) + ',\n'
                 for i in range(0, len(b), 16))
    s += '};\n'
(p / 'triangle_bytecode.h').write_text(s)
