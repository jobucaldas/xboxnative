#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device regression for the fixed-function pixel shader's texture stages;
# never contacts the console. Builds out/d3d9-translator/d3d9translator.dll from
# the pinned DXUP source, generates the ps_3_0 the D3D9 shim's fixed-function
# path emits (ffp.h writePixelShader) for two stage configurations, translates
# both under Wine, and asserts what the DXBC declares:
#
#   - textureless (the d3d9-triangle fixture's stage: SELECTARG1 with
#     D3DTA_DIFFUSE args, no texture set): NO dcl_sampler, NO dcl_resource and
#     NO sample instruction. Declaring and sampling a resource nothing binds
#     is what the console's d3d11on12->dxilconv path faulted on
#     (0xc0000005 at dxilconv+0x18a20, "Adding an implicit 2D sampler." in
#     the log just before).
#   - textured (a stage whose COLORARG1 is D3DTA_TEXTURE): the sampler and
#     resource declarations stay, and the sample does too - the fix must not
#     take the textured path's sampling away.
#
# The DXBC numbering is ground-truthed against the pinned donor's
# d3d11TokenizedProgramFormat.hpp as in test-positiont.sh: DCL_RESOURCE=88,
# DCL_SAMPLER=90, SAMPLE=69.
#
# Usage: src/tools/d3d9-translator/test-ffp-no-texture.sh [outdir]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
out=${1:-$root/out/d3d9-translator}
bash "$root/src/tools/build-d3d9-translator.sh" "$out"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The driver includes the shim's own ffp.h, so the tokens this test translates
# are the tokens the shim generates - not a hand-copy that can drift. It writes
# each configuration's ps_3_0 token stream to a file for translation.
cat >"$work/driver.cpp" <<'EOF'
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include "ffp.h"
int main() {
    // The d3d9-triangle fixture's stage, as d3d9.cpp builds the PixelOptions
    // from its texture-stage state: one stage, SELECTARG1 on the diffuse, the
    // D3D9 defaults for the second args (D3DTA_CURRENT), no texcoords.
    ffp::PixelOptions textureless{};
    textureless.stageCount=1;
    textureless.stages[0].colorOp=ffp::TopSelectArg1;
    textureless.stages[0].colorArg1=ffp::TaDiffuse;
    textureless.stages[0].colorArg2=ffp::TaCurrent;
    textureless.stages[0].alphaOp=ffp::TopSelectArg1;
    textureless.stages[0].alphaArg1=ffp::TaDiffuse;
    textureless.stages[0].alphaArg2=ffp::TaCurrent;
    // A textured stage: COLORARG1/ALPHAARG1 name the texture, one TEXCOORD
    // input supplies the coordinate.
    ffp::PixelOptions textured{};
    textured.stageCount=1;
    textured.texcoordCount=1;
    textured.texcoordReg[0]=2;
    textured.stages[0].colorOp=ffp::TopModulate;
    textured.stages[0].colorArg1=ffp::TaTexture;
    textured.stages[0].colorArg2=ffp::TaCurrent;
    textured.stages[0].alphaOp=ffp::TopModulate;
    textured.stages[0].alphaArg1=ffp::TaTexture;
    textured.stages[0].alphaArg2=ffp::TaCurrent;
    struct Case { const char* path; const ffp::PixelOptions* options; };
    const Case cases[]={
        {"ps-textureless.dx9",&textureless},
        {"ps-textured.dx9",&textured},
    };
    for(const Case& c:cases) {
        DWORD tokens[1024];
        const unsigned words=ffp::writePixelShader(*c.options,tokens,1024);
        if(!words){printf("FAIL writePixelShader returned 0 for %s\n",c.path);return 1;}
        FILE* f=fopen(c.path,"wb");
        if(!f){printf("FAIL cannot write %s\n",c.path);return 1;}
        fwrite(tokens,4,words,f);fclose(f);
        printf("wrote %s words=%u\n",c.path,words);
    }
    return 0;
}
EOF
cp "$here/../../xbox-test/uwph/d3d9/ffp.h" "$work/ffp.h"

# Reads the translated DXBC and fails when a sampler or resource is declared,
# or a sample instruction appears, for a shader that must not touch a texture.
cat >"$work/check-forbidden.py" <<'EOF'
import struct, sys
path, allow = sys.argv[1], sys.argv[2] == "allow"
data = open(path, "rb").read()
if data[:4] != b"DXBC":
    sys.exit("not a DXBC container")
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
DCL_RESOURCE, DCL_SAMPLER, SAMPLE = 88, 90, 69
found = {"dcl_resource": False, "dcl_sampler": False, "sample": False}
i = 2   # skip the version token and the dword count
while i < len(code):
    token = code[i]
    opcode = token & 0x7FF
    length = (token >> 24) & 0x7F
    if opcode == DCL_RESOURCE:
        found["dcl_resource"] = True
    elif opcode == DCL_SAMPLER:
        found["dcl_sampler"] = True
    elif opcode == SAMPLE:
        found["sample"] = True
    i += max(length, 1)
if allow:
    if not (found["dcl_resource"] and found["dcl_sampler"] and found["sample"]):
        sys.exit("the textured shader lost its sampling: %s" % found)
    print("ok: textured keeps dcl_resource+dcl_sampler+sample")
else:
    for name, present in found.items():
        if present:
            sys.exit("the textureless shader declares %s - the shape that faults dxilconv" % name)
    print("ok: textureless has no dcl_resource/dcl_sampler/sample")
EOF

# The driver and the translator caller are ordinary mingw programs with the
# host CRT, as test-positiont.sh's driver.
cat >"$work/translate.cpp" <<'EOF'
#include <windows.h>
#include <cstdio>
#include <cstdlib>
static unsigned char* readFile(const char* path,long* size) {
    FILE* f=fopen(path,"rb");if(!f)return nullptr;
    fseek(f,0,SEEK_END);*size=ftell(f);fseek(f,0,SEEK_SET);
    auto* data=(unsigned char*)malloc(*size);fread(data,1,*size,f);fclose(f);return data;
}
int main(int argc,char** argv) {
    if(argc<4){printf("usage: translate <translator.dll> <in.dx9> <out.dxbc>\n");return 2;}
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
    return 0;
}
EOF
# mingw-w64 on NixOS keeps libmcfgthread outside the compiler's search path, so
# its directory is passed when it exists; CI's Debian/Ubuntu mingw has the
# static runtime in its default search path already (as swap-chain-convert.sh).
extra=()
for d in /nix/store/*-mcfgthread-x86_64-w64-mingw32-*/lib; do [[ -d $d ]] && extra+=("-L$d"); done
build() { # build OUT SRC...
    local out=$1; shift
    x86_64-w64-mingw32-g++ -O2 -std=c++17 -o "$out" "$@" "${extra[@]}" -lkernel32 -static
}
build "$work/driver.exe" "$work/driver.cpp"
build "$work/translate.exe" "$work/translate.cpp"
cd "$work"
WINEDEBUG=-all WINEDLLOVERRIDES='winedbg.exe=d' timeout -k 2 60 wine ./driver.exe | tee run.log
grep -q 'wrote ps-textureless.dx9' run.log || { echo "FAIL the textureless tokens were not written" >&2; exit 1; }
for case in textureless:no textured:allow; do
    name=${case%%:*}; allow=${case##*:}
    WINEDEBUG=-all WINEDLLOVERRIDES='winedbg.exe=d' timeout -k 2 60 wine ./translate.exe \
        "$out/d3d9translator.dll" "ps-$name.dx9" "ps-$name.dxbc" | tee "run-$name.log"
    grep -q 'translate ok bytes=' "run-$name.log" || { echo "FAIL the translator refused $name" >&2; exit 1; }
    python3 -I check-forbidden.py "ps-$name.dxbc" "$allow" || { echo "FAIL the $name DXBC is wrong" >&2; exit 1; }
done
echo "ok: ffp pixel-shader texture stages"
