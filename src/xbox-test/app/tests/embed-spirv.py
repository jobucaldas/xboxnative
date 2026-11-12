#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Embed the checked-in SPIR-V without requiring a shader compiler at build time.
Mirrors src/xbox-test/uwph/host/shaders/embed.py."""
import sys
from pathlib import Path
here = Path(__file__).resolve().parent
src = Path(sys.argv[1]).resolve()
out = src / 'vulkan_spirv.h'
names = {'triangle.vert': 'vs', 'triangle.frag': 'fs', 'scale.comp': 'cs'}
s = '// Generated from src/xbox-test/uwph/vulkan/spirv/*.spv; regenerate with\n// src/xbox-test/uwph/vulkan/embed-spirv.py. SPIR-V 1.0 (Vulkan 1.0), compiled by\n// glslang from the .vert/.frag/.comp sources checked in beside the .spv files.\n'
for f in sorted(src.glob('*.spv')):
    stem = f.stem
    if stem not in names:
        continue
    b = f.read_bytes()
    s += f'static const unsigned char vulkan_{names[stem]}_spv[] = {{\n'
    s += ''.join(' ' + ','.join(f'0x{x:02x}' for x in b[i:i+16]) + ',\n'
                 for i in range(0, len(b), 16))
    s += '};\n'
out.write_text(s)
print(f'wrote {out} ({len(s)} bytes)')
