#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device regression for the D3D9 translator's POSITIONT handling; never
# contacts the console. Builds out/d3d9-translator/d3d9translator.dll from the
# pinned DXUP source, then drives its one export under Wine (kernel32 only, no
# d3d11: CI runners have no display) with the vs_3_0 the D3D9 shim's
# fixed-function path emits for D3DFVF_XYZRHW|DIFFUSE (ffp.h writeVertexShader
# with positionTransformed): dcl_positiont v0, dcl_color v1, the viewport
# transform from c5/c6 into o0, the colour pass-through into o1.
#
# On the console the DXBC->DXIL converter (dxilconv.dll) faulted on that
# shader: the translator emitted the ISGN element named "POSITIONT" with an
# undefined system value and a plain dcl_input in SHEX, and the converter
# dereferenced a null semantic resolving it. The fix
# (patches/dxup-src/0002-play-positiont-siv.patch) makes both carry the
# system value SV_Position while keeping the ISGN name string "POSITIONT".
# This test asserts exactly that in the translated DXBC:
#   - ISGN element [0] is "POSITIONT" on v0 with systemValueType
#     D3D_NAME_POSITION (1), and element [1] is "COLOR" on v1, undefined;
#   - SHEX declares v0 with dcl_input_siv and system value SV_Position
#     (name 1), and never with a plain dcl_input.
# The hard-coded opcode and name numbers are ground-truthed against the
# pinned donor's src/extern/microsoft/d3d11TokenizedProgramFormat.hpp
# (D3D10_SB_OPCODE enum: DCL_INPUT=95, DCL_INPUT_SIV=97; D3D10_SB_NAME:
# POSITION=1) and mingw's <d3dcommon.h> (D3D_NAME_POSITION=1).
#
# Usage: src/tools/d3d9-translator/test-positiont.sh [outdir]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
out=${1:-$root/out/d3d9-translator}
bash "$root/src/tools/build-d3d9-translator.sh" "$out"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# The driver is an ordinary mingw program: it only has to load the DLL and
# call it, so it may use the host CRT. It deliberately does not touch d3d11 -
# the DXBC content is what this test is about, and check-positiont.py reads
# it directly.
cat >"$work/driver.cpp" <<'EOF'
#include <windows.h>
#include <cstdio>
#include <cstdlib>
static unsigned char* readFile(const char* path,long* size) {
    FILE* f=fopen(path,"rb");if(!f)return nullptr;
    fseek(f,0,SEEK_END);*size=ftell(f);fseek(f,0,SEEK_SET);
    auto* data=(unsigned char*)malloc(*size);fread(data,1,*size,f);fclose(f);return data;
}
int main(int argc,char** argv) {
    if(argc<4){printf("usage: driver <translator.dll> <in.dx9> <out.dxbc>\n");return 2;}
    HMODULE module=LoadLibraryA(argv[1]);
    if(!module){printf("FAIL LoadLibrary err=%lu\n",GetLastError());return 1;}
    auto translate=(int(*)(const unsigned*,unsigned*,unsigned,unsigned*))
        GetProcAddress(module,"d3d9Translate");
    if(!translate){printf("FAIL no d3d9Translate export\n");return 1;}
    long size=0;unsigned char* code=readFile(argv[2],&size);
    if(!code){printf("FAIL cannot read %s\n",argv[2]);return 1;}
    static unsigned buffer[16384];unsigned bytes=0;
    int rc=translate((const unsigned*)code,buffer,sizeof buffer/sizeof buffer[0],&bytes);
    if(rc){printf("FAIL translate rc=%d\n",rc);return 1;}
    FILE* f=fopen(argv[3],"wb");fwrite(buffer,1,bytes,f);fclose(f);
    printf("translate ok bytes=%u\n",bytes);
    // The same input again, in the same process: the translator resets one
    // ShaderCodeTranslator per call, so anything the reset misses makes the
    // second answer differ (see test.sh for the case that caught this).
    static unsigned second[16384];unsigned secondBytes=0;
    rc=translate((const unsigned*)code,second,sizeof second/sizeof second[0],&secondBytes);
    if(rc){printf("FAIL second translate rc=%d\n",rc);return 1;}
    if(secondBytes!=bytes){printf("FAIL second translate bytes=%u first=%u\n",secondBytes,bytes);return 1;}
    for(unsigned i=0;i<bytes/4;++i)if(second[i]!=buffer[i]){printf("FAIL second translate differs at word %u\n",i);return 1;}
    printf("translate repeat ok bytes=%u\n",secondBytes);
    return 0;
}
EOF
# The vs_3_0 the shim's fixed-function path emits for D3DFVF_XYZRHW|DIFFUSE,
# token for token as ffp.h writeVertexShader builds it: inputs POSITIONT v0
# and COLOR v1, outputs o0 (position, viewport-transformed from c5/c6) and
# o1 (colour pass-through).
cat >"$work/make-fixture.py" <<'EOF'
import struct, sys
def op(code, *operands):
    # D3D9 instruction token: opcode | operand count in bits 24-27. Without
    # it the translator reads the next token as an operand and desynchronises.
    return [code | (len(operands) << 24)] + list(operands)
def dst(regnum, regtype, mask=0xF):
    return (regnum & 0x7FF) | ((regtype & 0x7) << 28) | (((regtype >> 3) & 0x3) << 11) | ((mask & 0xF) << 16)
def src(regnum, regtype, swizzle=0xE4):
    return (regnum & 0x7FF) | ((regtype & 0x7) << 28) | (((regtype >> 3) & 0x3) << 11) | ((swizzle & 0xFF) << 16)
# D3DSPR_* (d3d9types.h D3DSHADER_PARAM_REGISTER_TYPE) and the usages the
# shim declares, as in ffp.h.
RegInput, RegConst, RegOutput = 1, 2, 6
D3DDECLUSAGE_POSITION, D3DDECLUSAGE_POSITIONT, D3DDECLUSAGE_COLOR = 0, 9, 10
D3DSIO_MOV, D3DSIO_ADD, D3DSIO_MUL, D3DSIO_DCL, D3DSIO_END = 1, 2, 5, 0x1F, 0xFFFF
def dcl(usage, index, reg, regtype):
    return op(D3DSIO_DCL, usage | (index << 16), dst(reg, regtype))
tokens = [0xFFFE0300]                                   # vs_3_0
tokens += dcl(D3DDECLUSAGE_POSITIONT, 0, 0, RegInput)   # dcl_positiont v0
tokens += dcl(D3DDECLUSAGE_COLOR, 0, 1, RegInput)       # dcl_color v1
tokens += dcl(D3DDECLUSAGE_POSITION, 0, 0, RegOutput)   # dcl_position o0
tokens += dcl(D3DDECLUSAGE_COLOR, 0, 1, RegOutput)      # dcl_color o1
# o0.xy = v0.xy * c5.xy + c6.xy (viewport fixup), o0.zw = v0.zw, o1 = v1,
# exactly the positionTransformed branch of ffp.h writeVertexShader.
tokens += op(D3DSIO_MUL, dst(0, RegOutput, 0x1), src(0, RegInput, 0x00), src(5, RegConst, 0x00))
tokens += op(D3DSIO_ADD, dst(0, RegOutput, 0x1), src(0, RegOutput, 0x00), src(6, RegConst, 0x00))
tokens += op(D3DSIO_MUL, dst(0, RegOutput, 0x2), src(0, RegInput, 0x55), src(5, RegConst, 0x55))
tokens += op(D3DSIO_ADD, dst(0, RegOutput, 0x2), src(0, RegOutput, 0x55), src(6, RegConst, 0x55))
tokens += op(D3DSIO_MOV, dst(0, RegOutput, 0xC), src(0, RegInput, 0xEE))
tokens += op(D3DSIO_MOV, dst(1, RegOutput), src(1, RegInput))
tokens += [D3DSIO_END]
open(sys.argv[1], "wb").write(struct.pack("<%dI" % len(tokens), *tokens))
EOF
# Reads the DXBC signatures the donor wrote (dxbc_chunks.cpp IOSGNChunk and
# SHEXChunk::writeIODcls) and asserts the POSITIONT input carries the system
# value in both. Numbering ground-truthed against the pinned donor's
# d3d11TokenizedProgramFormat.hpp, see the header comment.
cat >"$work/check-positiont.py" <<'EOF'
import struct, sys
data = open(sys.argv[1], "rb").read()
if data[:4] != b"DXBC":
    sys.exit("not a DXBC container")
count = struct.unpack_from("<I", data, 28)[0]
offsets = {}
for i in range(count):
    off = struct.unpack_from("<I", data, 32 + 4 * i)[0]
    offsets[data[off:off + 4]] = off
# ISGN: fourcc, size, element count, the constant 8, then 6-dword elements
# (nameOffset, semanticIndex, systemValueType, componentType, registerIndex,
# mask), then the aligned name strings. A name offset is relative to the
# chunk data start, i.e. after the 8-byte chunk header.
off = offsets.get(b"ISGN")
if off is None:
    sys.exit("no ISGN chunk")
n = struct.unpack_from("<I", data, off + 8)[0]
def elementName(nameOffset):
    p = off + 8 + nameOffset
    return data[p:data.index(b"\0", p)].decode()
elements = [struct.unpack_from("<6I", data, off + 16 + 24 * i) for i in range(n)]
D3D_NAME_UNDEFINED, D3D_NAME_POSITION = 0, 1
if n != 2:
    sys.exit("ISGN has %d elements, expected 2 (POSITIONT v0, COLOR v1)" % n)
pos, col = elements
if elementName(pos[0]) != "POSITIONT" or pos[1] != 0 or pos[2] != D3D_NAME_POSITION or pos[4] != 0:
    sys.exit("ISGN[0] is name=%s semantic=%d sysval=%d reg=%d, expected POSITIONT/0/SV_Position(1)/0"
             % (elementName(pos[0]), pos[1], pos[2], pos[4]))
if elementName(col[0]) != "COLOR" or col[2] != D3D_NAME_UNDEFINED or col[4] != 1:
    sys.exit("ISGN[1] is name=%s sysval=%d reg=%d, expected COLOR/UNDEFINED(0)/1"
             % (elementName(col[0]), col[2], col[4]))
print("ISGN ok: [0] POSITIONT sysval=1 reg=0, [1] COLOR sysval=0 reg=1")
# SHEX: version token, dword count, then instructions. Operand token: register
# type in bits 12-19, index dimension in bits 20-21, first index
# representation in bits 24-26.
off = offsets.get(b"SHEX")
if off is None:
    sys.exit("no SHEX chunk")
size = struct.unpack_from("<I", data, off + 4)[0]
code = struct.unpack_from("<%dI" % (size // 4), data, off + 8)
D3D10_SB_OPCODE_DCL_INPUT, D3D10_SB_OPCODE_DCL_INPUT_SIV = 95, 97
D3D10_SB_OPERAND_TYPE_INPUT, D3D10_SB_NAME_POSITION = 1, 1
siv = False
i = 2   # skip the version token and the dword count
while i < len(code):
    token = code[i]
    opcode = token & 0x7FF
    length = (token >> 24) & 0x7F
    if opcode in (D3D10_SB_OPCODE_DCL_INPUT, D3D10_SB_OPCODE_DCL_INPUT_SIV):
        opnd = code[i + 1]
        regtype = (opnd >> 12) & 0xFF
        dimension = (opnd >> 20) & 0x3
        representation = (opnd >> 24) & 0x7
        regnum = code[i + 2]
        if regtype == D3D10_SB_OPERAND_TYPE_INPUT and dimension == 1 and representation == 0 and regnum == 0:
            if opcode == D3D10_SB_OPCODE_DCL_INPUT_SIV and code[i + 3] == D3D_NAME_POSITION:
                siv = True
            else:
                sys.exit("v0 is declared with opcode %d (name %d), expected dcl_input_siv SV_Position"
                         % (opcode, code[i + 3] if opcode == D3D10_SB_OPCODE_DCL_INPUT_SIV else -1))
    i += max(length, 1)
if not siv:
    sys.exit("SHEX never declares v0 with dcl_input_siv SV_Position")
print("SHEX ok: dcl_input_siv v0, SV_Position")
EOF
python3 -I "$work/make-fixture.py" "$work/vs30-positiont.bin"
x86_64-w64-mingw32-g++ -O2 -std=c++17 "$work/driver.cpp" -o "$work/driver.exe" \
    -lkernel32 -static 2>/dev/null \
  || { # mingw-w64 on NixOS keeps libmcfgthread outside the compiler's search path
       mcfg=$(dirname "$(ls /nix/store/*mcfgthread*x86_64-w64-mingw32*/lib/libmcfgthread.a 2>/dev/null | head -1)")
       [[ -n $mcfg ]] || { echo "cannot link the driver: libmcfgthread not found" >&2; exit 2; }
       x86_64-w64-mingw32-g++ -O2 -std=c++17 "$work/driver.cpp" -o "$work/driver.exe" \
           -L"$mcfg" -lkernel32 -static; }
cd "$work"
WINEDEBUG=-all WINEDLLOVERRIDES='winedbg.exe=d' timeout -k 2 120 wine ./driver.exe \
    "$out/d3d9translator.dll" vs30-positiont.bin out.dxbc | tee run.log
grep -q 'translate ok bytes=' run.log || { echo "FAIL translator produced no DXBC" >&2; exit 1; }
grep -q 'translate repeat ok bytes=' run.log || { echo "FAIL the POSITIONT shader translates differently on its second call" >&2; exit 1; }
python3 -I check-positiont.py out.dxbc || { echo "FAIL the translated POSITIONT signatures are wrong" >&2; exit 1; }
echo "ok: translator POSITIONT ISGN/SIV"
