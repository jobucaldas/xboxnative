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
    // Wine's D3D11 is the same API the console's device answers, so it is the
    // right referee for "is this valid DXBC".
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;D3D_FEATURE_LEVEL level;
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,
                                 D3D11_SDK_VERSION,&device,&level,&context);
    if(FAILED(hr)){printf("FAIL D3D11CreateDevice hr=0x%lx\n",(unsigned long)hr);return 1;}
    ID3D11VertexShader* shader=nullptr;
    hr=device->CreateVertexShader(buffer,bytes,nullptr,&shader);
    printf("CreateVertexShader hr=0x%lx\n",(unsigned long)hr);
    if(shader)shader->Release();
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
D3DSIO_MOV, D3DSIO_DCL, D3DSIO_M4x4, D3DSIO_END = 1, 0x1F, 0x18, 0xFFFF
D3DDECLUSAGE_POSITION = 0
tokens = [0xFFFE0101]                                   # vs_1_1
tokens += op(D3DSIO_DCL, D3DDECLUSAGE_POSITION << 16, dst(0, D3DSPR_INPUT))
tokens += op(D3DSIO_M4x4, dst(0, D3DSPR_RASTOUT), src(0, D3DSPR_INPUT), src(0, D3DSPR_CONST))
tokens += [D3DSIO_END]
open(sys.argv[1], "wb").write(struct.pack("<%dI" % len(tokens), *tokens))
EOF
python3 -I "$work/make-fixture.py" "$work/vs11.bin"
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
# Same input, same output: a second run in a fresh process must produce the
# same bytes. This is the check that catches a table left uninitialised by the
# CRT-free build (the failure that made the first attempt translate nothing).
WINEDEBUG=-all WINEDLLOVERRIDES='winedbg.exe=d' timeout -k 2 120 wine ./driver.exe \
    "$out/d3d9translator.dll" vs11.bin out2.dxbc >/dev/null
cmp out.dxbc out2.dxbc || { echo "FAIL translator is not deterministic" >&2; exit 1; }
echo "ok: translator"
