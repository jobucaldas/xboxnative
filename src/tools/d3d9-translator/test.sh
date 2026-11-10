#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device regression for the D3D9 shader translator; never contacts the
# console. Builds out/d3d9-translator/d3d9translator.dll from the pinned DXUP
# source, then drives its one export against Wine's d3d11.dll:
#   1. a hand-written vs_1_1 translates to DXBC that Wine's D3D11 accepts, and
#   2. the same blob translates to the same bytes twice (the translator's
#      global tables are rebuilt per process, so this also proves they are not
#      left uninitialised by the CRT-free build).
# Usage: src/tools/d3d9-translator/test.sh [outdir]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
out=${1:-$root/out/d3d9-translator}
bash "$root/src/tools/build-d3d9-translator.sh" "$out"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# The driver is an ordinary mingw program: it only has to load the DLL and call
# it, so it may use the host CRT.
cat >"$work/driver.cpp" <<'EOF'
#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdlib>
static unsigned char* readFile(const char* path,long* size) {
    FILE* f=fopen(path,"rb");if(!f)return nullptr;
    fseek(f,0,SEEK_END);*size=ftell(f);fseek(f,0,SEEK_SET);
    auto* data=(unsigned char*)malloc(*size);fread(data,1,*size,f);fclose(f);return data;
}
int main(int argc,char** argv) {
    if(argc<4){printf("usage: driver <translator.dll> <in.dx9> <out.dxbc> [ps]\n");return 2;}
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
    // The same input again, in the same process: the translator keeps one
    // ShaderCodeTranslator and resets it per call, so anything the reset misses
    // (an uninitialised operand word, a table entry added by the previous
    // shader) makes the second answer differ. A game creates its shaders in one
    // process, so this is the case that matters.
    static unsigned second[16384];unsigned secondBytes=0;
    rc=translate((const unsigned*)code,second,sizeof second/sizeof second[0],&secondBytes);
    if(rc){printf("FAIL second translate rc=%d\n",rc);return 1;}
    if(secondBytes!=bytes){printf("FAIL second translate bytes=%u first=%u\n",secondBytes,bytes);return 1;}
    for(unsigned i=0;i<bytes/4;++i)if(second[i]!=buffer[i]){printf("FAIL second translate differs at word %u\n",i);return 1;}
    printf("translate repeat ok bytes=%u\n",secondBytes);
    // Wine's D3D11 is the same API the console's device answers, so it is the
    // right referee for "is this valid DXBC".
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;D3D_FEATURE_LEVEL level;
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,
                                 D3D11_SDK_VERSION,&device,&level,&context);
    if(FAILED(hr)){printf("FAIL D3D11CreateDevice hr=0x%lx\n",(unsigned long)hr);return 1;}
    // The caller names the stage: the blob is a whole DXBC container, so its
    // first word is "DXBC" rather than the program's version token.
    const bool pixel=argc>4&&argv[4][0]=='p';
    if(pixel) {
        ID3D11PixelShader* shader=nullptr;
        hr=device->CreatePixelShader(buffer,bytes,nullptr,&shader);
        printf("CreatePixelShader hr=0x%lx\n",(unsigned long)hr);
        if(shader)shader->Release();
    } else {
        ID3D11VertexShader* shader=nullptr;
        hr=device->CreateVertexShader(buffer,bytes,nullptr,&shader);
        printf("CreateVertexShader hr=0x%lx\n",(unsigned long)hr);
        if(shader)shader->Release();
    }
    context->Release();device->Release();
    return SUCCEEDED(hr)?0:1;
}
EOF
# A minimal vs_1_1: dcl_position v0; m4x4 oPos, v0, c0. Hand-written tokens, so
# the fixture does not depend on a shader compiler being installed. No `ret`:
# the donor appends the DXBC RET itself and treats an explicit one as an
# unimplemented operation.
cat >"$work/make-fixture.py" <<'EOF'
import struct, sys
def op(code, *operands):
    # D3DSHADER_INSTRUCTION_OPCODE_TYPE | D3DSI_INSTLENGTH_MASK: the operand
    # count is in bits 24-27. Leaving it out makes the translator read the next
    # token as an operand and desynchronise.
    return [code | (len(operands) << 24)] + list(operands)
def dst(regnum, regtype, mask=0xF):
    # D3DSP_DSTMOD 0, D3DSP_DSTSHIFT 0, D3DSP_REGTYPE, D3DSP_REGNUM
    return (regnum & 0x7FF) | ((regtype & 0x7) << 28) | ((mask & 0xF) << 16)
def src(regnum, regtype, swizzle=0xE4):
    return (regnum & 0x7FF) | ((regtype & 0x7) << 28) | ((swizzle & 0xFF) << 16)
D3DSPR_TEMP, D3DSPR_INPUT, D3DSPR_CONST, D3DSPR_ADDR, D3DSPR_RASTOUT = 0, 1, 2, 3, 4
D3DSIO_MOV, D3DSIO_DCL, D3DSIO_M4x4, D3DSIO_END = 1, 0x1F, 20, 0xFFFF   # d3d9types.h: m4x4 is 20, not 0x18 (m3x2)
D3DDECLUSAGE_POSITION = 0
tokens = [0xFFFE0101]                                   # vs_1_1
tokens += op(D3DSIO_DCL, D3DDECLUSAGE_POSITION << 16, dst(0, D3DSPR_INPUT))
tokens += op(D3DSIO_M4x4, dst(0, D3DSPR_RASTOUT), src(0, D3DSPR_INPUT), src(0, D3DSPR_CONST))
tokens += [D3DSIO_END]
open(sys.argv[1], "wb").write(struct.pack("<%dI" % len(tokens), *tokens))
EOF
# The translated matrix must write all four components. The donor expanded a
# matrix operation into one DP4 per column but copied the destination's whole
# write mask onto each, so only the last row survived and every transformed
# vertex landed outside the guard band - a shader D3D11 accepts happily, which
# is why this is checked in the bytecode and not just by CreateVertexShader.
cat >"$work/check-matrix.py" <<'EOF'
import struct, sys
data = open(sys.argv[1], "rb").read()
count = struct.unpack_from("<I", data, 28)[0]
shex = None
for i in range(count):
    off = struct.unpack_from("<I", data, 32 + 4 * i)[0]
    if data[off:off + 4] == b"SHEX":
        shex = off
if shex is None:
    sys.exit("no SHEX chunk")
size = struct.unpack_from("<I", data, shex + 4)[0]
code = struct.unpack_from("<%dI" % (size // 4), data, shex + 8)
D3D10_SB_OPCODE_DP4 = 0x11
masks = []
i = 0
while i < len(code):
    token = code[i]
    length = (token >> 24) & 0x7F
    if (token & 0x7FF) == D3D10_SB_OPCODE_DP4:
        masks.append((code[i + 1] >> 4) & 0xF)   # dst write mask, one bit per component
    i += max(length, 1)
if masks != [0x1, 0x2, 0x4, 0x8]:
    sys.exit("matrix rows wrote masks %s, expected [1, 2, 4, 8]" % masks)
print("matrix rows ok: %s" % masks)
EOF
python3 -I "$work/make-fixture.py" "$work/vs11.bin"
# A ps_1_1 with no dcl at all, exactly as a game's SM1 pixel shader looks:
# `tex t0` with a destination operand the donor's table gives no tokens for.
# That operand's token word was uninitialised, so isIndirect() read whatever the
# previous shader left in the translator and the shader could come out declaring
# a 288-register indirect constant buffer instead of none.
cat >"$work/make-ps.py" <<'EOF'
import struct, sys
def op(code, *operands):
    return [code | (len(operands) << 24)] + list(operands)
def dst(regnum, regtype, mask=0xF):
    return (regnum & 0x7FF) | ((regtype & 0x7) << 28) | ((mask & 0xF) << 16)
def src(regnum, regtype, swizzle=0xE4):
    return (regnum & 0x7FF) | ((regtype & 0x7) << 28) | ((swizzle & 0xFF) << 16)
D3DSPR_TEMP, D3DSPR_INPUT, D3DSPR_ADDR = 0, 1, 3
D3DSIO_MUL, D3DSIO_TEX, D3DSIO_END = 5, 0x42, 0xFFFF
tokens = [0xFFFF0101]                                   # ps_1_1
tokens += op(D3DSIO_TEX, dst(0, D3DSPR_ADDR))           # tex t0
tokens += op(D3DSIO_MUL, dst(0, D3DSPR_TEMP), src(0, D3DSPR_ADDR), src(0, D3DSPR_INPUT))
tokens += [D3DSIO_END]
open(sys.argv[1], "wb").write(struct.pack("<%dI" % len(tokens), *tokens))
EOF
python3 -I "$work/make-ps.py" "$work/ps11.bin"
x86_64-w64-mingw32-g++ -O2 -std=c++17 "$work/driver.cpp" -o "$work/driver.exe" \
    -lkernel32 -ld3d11 -static 2>/dev/null \
  || { # mingw-w64 on NixOS keeps libmcfgthread outside the compiler's search path
       mcfg=$(dirname "$(ls /nix/store/*mcfgthread*x86_64-w64-mingw32*/lib/libmcfgthread.a 2>/dev/null | head -1)")
       [[ -n $mcfg ]] || { echo "cannot link the driver: libmcfgthread not found" >&2; exit 2; }
       x86_64-w64-mingw32-g++ -O2 -std=c++17 "$work/driver.cpp" -o "$work/driver.exe" \
           -L"$mcfg" -lkernel32 -ld3d11 -static; }
# Wine serves its own builtin for a DLL it recognises by name; the translator's
# name is not one it knows, but force native loading anyway so the test reads
# the same export table Play will.
cd "$work"
WINEDEBUG=-all WINEDLLOVERRIDES='winedbg.exe=d' timeout -k 2 120 wine ./driver.exe \
    "$out/d3d9translator.dll" vs11.bin out.dxbc | tee run.log
grep -q 'translate ok bytes=' run.log || { echo "FAIL translator produced no DXBC" >&2; exit 1; }
grep -q 'CreateVertexShader hr=0x0' run.log || { echo "FAIL Wine's D3D11 rejected the translated DXBC" >&2; exit 1; }
python3 -I check-matrix.py out.dxbc || { echo "FAIL the translated matrix does not write every row" >&2; exit 1; }
# And a pixel shader's translation is the same on its first and second call in
# one process (the driver checks that itself; this run gives it a shader whose
# operand table entry has no tokens).
WINEDEBUG=-all WINEDLLOVERRIDES='winedbg.exe=d' timeout -k 2 120 wine ./driver.exe \
    "$out/d3d9translator.dll" ps11.bin out-ps.dxbc ps | tee ps.log
grep -q 'translate repeat ok bytes=' ps.log || { echo "FAIL a pixel shader translates differently on its second call" >&2; exit 1; }
grep -q 'CreatePixelShader hr=0x0' ps.log || { echo "FAIL Wine's D3D11 rejected the translated pixel shader" >&2; exit 1; }
# Same input, same output: a second run in a fresh process must produce the
# same bytes. This is the check that catches a table left uninitialised by the
# CRT-free build (the failure that made the first attempt translate nothing).
WINEDEBUG=-all WINEDLLOVERRIDES='winedbg.exe=d' timeout -k 2 120 wine ./driver.exe \
    "$out/d3d9translator.dll" vs11.bin out2.dxbc >/dev/null
cmp out.dxbc out2.dxbc || { echo "FAIL translator is not deterministic" >&2; exit 1; }
echo "ok: translator"
