# D3D10 fixture shaders (Shader Model 4.0)

The `d3d10-triangle` fixture (src/xbox-test/app/tests/play-d3d10-triangle.cpp)
draws with these blobs. They are SM4 (vs_4_0/ps_4_0/gs_4_0), the shader model a
D3D10 device runs — the D3D11 fixture's SM5 blobs would be refused — committed
as DXBC so nothing compiles shaders at build or run time.

The source is embedded in `compile40.cpp` (one file, one `main`): the triangle
pair the D3D11 fixture draws plus the point-expansion geometry shader the
D3D11 fixture's `gs_point.hlsl` holds, all in one compilation unit so the
signatures are shared by construction.

Regenerate on Linux (same method as the D3D11 shaders' README):

```sh
x86_64-w64-mingw32-g++ compile40.cpp -static -ld3dcompiler -o /tmp/compile40.exe
wine /tmp/compile40.exe          # writes d3d10_{vs,ps,gs}.dxbc here
# then regenerate the header:
python3 - <<'EOF'
from pathlib import Path
out = []
out.append('// Generated from src/tools/d3d10-shaders/*.dxbc (vs_4_0/ps_4_0/gs_4_0,\n')
out.append('// compiled with Wine d3dcompiler_47 by compile40.cpp there); regenerate\n')
out.append('// with that script. Same format as the D3D11 fixture\'s triangle_bytecode.h.\n')
for name in ('vs','ps','gs'):
    b = Path(f'd3d10_{name}.dxbc').read_bytes()
    out.append(f'static const unsigned char d3d10_{name}[] = {{\n')
    for i in range(0, len(b), 16):
        out.append(' ' + ','.join(f'0x{x:02x}' for x in b[i:i+16]) + ',\n')
    out.append('};\n')
(src := Path('../xbox-test/app/tests/d3d10_bytecode.h')).write_text(''.join(out))
EOF
```
