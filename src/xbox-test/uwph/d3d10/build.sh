#!/bin/bash
# Builds Play's D3D10 shim: one PE DLL mapped by Play's loader under the names
# d3d10.dll, d3d10_1.dll and d3d10core.dll (see d3d10.cpp).
#
# The shim links no CRT and no import library of the D3D DLLs it replaces: its
# only imports are d3d11.dll and dxgi.dll, which Play's loader routes to the
# host's real device and to its validated CoreWindow factory.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:?output directory required}"
mkdir -p "$out"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# The mingw import libraries live beside the compiler's own libkernel32.a; ask
# it rather than hardcoding a prefix.
mingw_lib=$(dirname "$(x86_64-w64-mingw32-g++ -print-file-name=libkernel32.a)")
# Keep the compiler from recognising this file's own memset and memcpy loops as
# the libc calls they implement: its loop-distribution pass rewrote the byte
# loop in memset() into a call to memset(), which then jumped to itself forever.
# -ffreestanding is deliberately not used: it hides malloc from the headers.
#
# Unwind tables are ON, deliberately: Play maps this DLL and runs it on the
# guest's thread, and a fault inside it can only be unwound to that thread's
# __try/__except through this image's .pdata. See src/xbox-test/uwph/d3d9/build.sh.
x86_64-w64-mingw32-g++ -c -O2 -std=c++17 -fno-builtin -fno-tree-loop-distribute-patterns \
    -fno-exceptions -fno-rtti -fno-stack-protector \
    -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type \
    "$here/d3d10.cpp" -o "$work/d3d10.obj"
# CI installs plain lld; development hosts here carry the versioned name.
link() { lld-link-19 "$@" 2>/dev/null || lld-link "$@"; }
link /dll /noentry /nodefaultlib /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat \
    /def:"$here/d3d10.def" /implib:"$work/d3d10.lib" /out:"$out/d3d10.dll" \
    "$work/d3d10.obj" /libpath:"$mingw_lib" libkernel32.a libuuid.a libd3d11.a libdxgi.a
# The image is only usable if Play can route everything it imports: the app
# partition supplies d3d11.dll and dxgi.dll (through Play's routes) and the
# kernel, and nothing else. A writable code section would fail W^X.
bad=$(llvm-readobj --coff-imports "$out/d3d10.dll" | awk '/^  Name: /{print tolower($2)}' | sort -u \
      | grep -Ev '^(kernel32|d3d11|dxgi)\.dll$' || true)
[[ -z $bad ]] || { echo "d3d10.dll imports DLLs Play does not route: $bad" >&2; exit 1; }
rwx=$(llvm-readobj --sections "$out/d3d10.dll" \
      | awk '/Name: \./{n=$2;w=0;x=0} /IMAGE_SCN_MEM_WRITE/{w=1} /IMAGE_SCN_MEM_EXECUTE/{x=1} \
             /^ *\]$/{if(w&&x)print n}')
[[ -z $rwx ]] || { echo "d3d10.dll has writable code section(s): $rwx" >&2; exit 1; }
for name in D3D10CreateDevice D3D10CreateDeviceAndSwapChain D3D10CreateDevice1 D3D10CreateStateBlock; do
    llvm-readobj --coff-exports "$out/d3d10.dll" | grep -q "Name: $name\$" \
        || { echo "d3d10.dll does not export $name" >&2; exit 1; }
done
# A fault inside this DLL must reach the guest thread's handler, which needs the
# image's unwind data (see src/xbox-test/uwph/d3d9/build.sh).
python3 -I "$here/../d3d9/check-unwind-data.py" "$out/d3d10.dll"
# d3d10_1.dll forwards to d3d10.dll and d3d10core.dll is the layer below it, so
# the same image answers all three names; Play maps it under whichever the guest
# asked for.
cp "$out/d3d10.dll" "$out/d3d10_1.dll"
cp "$out/d3d10.dll" "$out/d3d10core.dll"
