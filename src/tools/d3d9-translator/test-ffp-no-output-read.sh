#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device reproducer for the dxilconv.dll+0x18a20 null deref the console
# hits on selftest d3d9-triangle / d3d9-triangle-variants (0.5.275-302):
# the fixed-function vertex shader's POSITIONT viewport fixup read oPos back
# as a source ("mul oPos.x, v0.x, c5.x; add oPos.x, oPos.x, c6.x"). oPos is
# write-only in SM3 and output registers are write-only in SM4 DXBC, so the
# DXBC->DXIL converter dereferenced a null SSA value resolving that read.
# The passing d3d9-textured fixture's shaders never read an output.
#
# This test compiles the shim's real ffp.h emitters into a generator, emits
# every vertex shader the FFP path can produce (transformed and not, with
# and without texture coordinates), translates each with the real
# d3d9translator.dll under Wine, and asserts no translated DXBC ever reads
# an output register as a source. It failed before the fix (the POSITIONT
# variants read o0) and passes after.
#
# Usage: src/tools/d3d9-translator/test-ffp-no-output-read.sh [outdir]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
out=${1:-$root/out/d3d9-translator}
bash "$root/src/tools/build-d3d9-translator.sh" "$out"
work=$(mktemp -d)
trap '[[ ${KEEPWORK:-0} = 1 ]] || rm -rf "$work"' EXIT
# The generator includes the shim's ffp.h directly, so the token streams it
# writes are the ones the shim emits - no hand-copied copy to drift. ffp.h
# has no includes: it expects DWORD and D3DDECLUSAGE from the includer, and
# it only writes files, so it compiles with the host compiler and runs
# natively.
cat >"$work/generate.cpp" <<'EOF'
typedef unsigned int DWORD;
enum D3DDECLUSAGE { D3DDECLUSAGE_POSITION=0, D3DDECLUSAGE_COLOR=10,
    D3DDECLUSAGE_TEXCOORD=5, D3DDECLUSAGE_POSITIONT=9 };
#include <xbox-test/uwph/d3d9/ffp.h>
#include <cstdio>
#include <cstdlib>
using namespace ffp;
static void write(const char* path,const unsigned* tokens,unsigned count) {
    FILE* f=fopen(path,"wb");
    fwrite(tokens,4,count,f);
    fclose(f);
}
int main(int argc,char** argv) {
    if(argc<2){printf("usage: generate <outdir>\n");return 2;}
    const char* dir=argv[1];
    char path[512];
    // Vertex shader variants: (transformed, colour, texcoords). The
    // d3d9-triangle fixture is the first row; d3d9-triangle-variants adds
    // the texcoord rows.
    const struct { bool transformed; int colour; unsigned texcoords; } rows[]={
        {true, 0, 0},{true, 1, 0},{false, 0, 0},{false, 1, 2},
    };
    for(unsigned i=0;i<sizeof rows/sizeof rows[0];++i) {
        VertexOptions options{};
        Input inputs[3];
        unsigned n=0;
        inputs[n++]={D3DDECLUSAGE_POSITIONT,0,0};
        if(!rows[i].transformed) inputs[0]={D3DDECLUSAGE_POSITION,0,0};
        if(rows[i].colour) inputs[n++]={D3DDECLUSAGE_COLOR,0,1};
        for(unsigned t=0;t<rows[i].texcoords;++t) inputs[n++]={D3DDECLUSAGE_TEXCOORD,t,2+t};
        options.inputCount=n;
        for(unsigned k=0;k<n;++k) options.inputs[k]=inputs[k];
        options.colourReg=rows[i].colour?1:-1;
        options.texcoordCount=rows[i].texcoords;
        for(unsigned t=0;t<rows[i].texcoords;++t) options.texcoordReg[t]=2+t;
        options.positionTransformed=rows[i].transformed;
        static DWORD tokens[512];
        unsigned count=writeVertexShader(options,tokens,sizeof tokens/sizeof tokens[0]);
        if(!count){printf("FAIL vs%u overflow\n",i);return 1;}
        snprintf(path,sizeof path,"%s/vs%u.dx9",dir,i);
        write(path,tokens,count);
    }
    return 0;
}
EOF
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
    return 0;
}
EOF
# Walks every instruction of every SHEX chunk and fails when a source
# operand names an output register (type 2 in bits 12-19 of the operand
# token, D3D10_SB_OPERAND_TYPE_OUTPUT). Operand tokens are variable length:
# the index representation lives in bits 22-24, 3 bits per index dimension
# (donor line 923), and immediate32/64 operands carry extra dwords, so the
# walker sizes each operand instead of skipping a fixed word count.
# Numbering ground-truthed against the pinned donor's
# d3d11TokenizedProgramFormat.hpp.
cat >"$work/check-no-output-read.py" <<'EOF'
import struct, sys
failures = 0
for path in sorted(sys.argv[1:]):
    data = open(path, "rb").read()
    if data[:4] != b"DXBC":
        sys.exit("%s: not a DXBC container" % path)
    count = struct.unpack_from("<I", data, 28)[0]
    shex = None
    for i in range(count):
        off = struct.unpack_from("<I", data, 32 + 4 * i)[0]
        if data[off:off + 4] == b"SHEX":
            shex = off
    if shex is None:
        sys.exit("%s: no SHEX chunk" % path)
    size = struct.unpack_from("<I", data, shex + 4)[0]
    code = struct.unpack_from("<%dI" % (size // 4), data, shex + 8)
    D3D10_SB_OPERAND_TYPE_OUTPUT, D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH = 2, 12
    def operand_words(o):
        words = 0
        dim = (o >> 20) & 0x3
        for d in range(dim):
            rep = (o >> (22 + 3 * d)) & 0x3
            words += 1                      # the index dword itself
            if rep in (2, 3): words += 1    # relative: one extra operand token
            if rep == 4: words += 3
        t = (o >> 12) & 0xFF
        if t == 4: words += 4               # IMMEDIATE32
        if t == 5: words += 8               # IMMEDIATE64
        return words
    i = 2   # version token, dword count
    while i < len(code):
        token = code[i]
        opcode = token & 0x7FF
        length = (token >> 24) & 0x7F
        end = i + max(length, 1)
        if not (89 <= opcode <= 118):     # dcl_* opcodes carry no reads
            # Operand 0 of an ALU instruction is the write-only destination;
            # everything after it is a read. ret has no operands.
            p = i + 2
            while p < end:
                opnd = code[p]
                regtype = (opnd >> 12) & 0xFF
                if regtype in (D3D10_SB_OPERAND_TYPE_OUTPUT, D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH):
                    print("FAIL %s: opcode %d reads an output register as a source (operand token 0x%08x)"
                          % (path, opcode, opnd))
                    failures += 1
                    break
                p += 1 + operand_words(opnd)
        i += max(length, 1)
if failures:
    sys.exit(1)
print("ok: no translated shader reads an output register")
EOF
# The generator only writes files: compile and run it with the host tools.
g++ -O2 -std=c++17 -I"$root/src" "$work/generate.cpp" -o "$work/generate"
"$work/generate" "$work"
# The driver is a mingw PE: it loads the translator DLL and calls it under
# Wine. Same mcfgthread fallback as test-positiont.sh.
x86_64-w64-mingw32-g++ -O2 -std=c++17 "$work/driver.cpp" -o "$work/driver.exe" -lkernel32 -static 2>/dev/null \
  || { mcfg=$(dirname "$(ls /nix/store/*mcfgthread*x86_64-w64-mingw32*/lib/libmcfgthread.a 2>/dev/null | head -1)")
       [[ -n $mcfg ]] || { echo "cannot link the driver: libmcfgthread not found" >&2; exit 2; }
       x86_64-w64-mingw32-g++ -O2 -std=c++17 "$work/driver.cpp" -o "$work/driver.exe" -L"$mcfg" -lkernel32 -static; }
cd "$work"
for vs in vs*.dx9; do
    WINEDEBUG=-all timeout -k 2 120 wine ./driver.exe "$out/d3d9translator.dll" "$vs" "${vs%.dx9}.dxbc" | tee "run-$vs.log"
    grep -q 'translate ok bytes=' "run-$vs.log" || { echo "FAIL $vs produced no DXBC" >&2; exit 1; }
done
python3 -I check-no-output-read.py vs*.dxbc || { echo "FAIL a translated FFP shader reads an output register" >&2; exit 1; }
echo "ok: FFP shaders never read an output register"
