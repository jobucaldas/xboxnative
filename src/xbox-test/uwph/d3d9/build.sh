#!/bin/bash
# Builds Play's D3D9 shim: one PE DLL mapped by Play's loader under the name
# d3d9.dll (see d3d9.cpp and issue #172).
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
# Same flags as the D3D10 shim: no CRT, no exceptions, and the loop-distribution
# pass disabled so this file's own memset/memcpy loops do not become calls to
# themselves.
#
# Unwind tables are ON, deliberately. Play maps this DLL and runs it on the
# guest's thread; when code inside it faults, the OS has to unwind to that
# thread's __try/__except to reach Play's filter, and it can only do that
# through this image's .pdata. -fno-asynchronous-unwind-tables leaves the
# exception directory empty, the mapper accepts that silently (there is nothing
# to register), and a fault here is then never dispatched: the launcher dies
# with no crash record at all. The gate at the end of this script keeps it.
x86_64-w64-mingw32-g++ -c -O2 -std=c++17 -fno-builtin -fno-tree-loop-distribute-patterns \
    -fno-exceptions -fno-rtti -fno-stack-protector \
    -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type \
    "$here/d3d9.cpp" -o "$work/d3d9.obj"
# CI installs plain lld; development hosts here carry the versioned name.
link() { lld-link-19 "$@" 2>/dev/null || lld-link "$@"; }
link /dll /noentry /nodefaultlib /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat \
    /def:"$here/d3d9.def" /implib:"$work/d3d9.lib" /out:"$out/d3d9.dll" \
    "$work/d3d9.obj" /libpath:"$mingw_lib" libkernel32.a libuuid.a libd3d11.a libdxgi.a
# The image is only usable if Play can route everything it imports: the app
# partition supplies d3d11.dll and dxgi.dll (through Play's routes) and the
# kernel, and nothing else. A writable code section would fail W^X.
bad=$(llvm-readobj --coff-imports "$out/d3d9.dll" | awk '/^  Name: /{print tolower($2)}' | sort -u \
      | grep -Ev '^(kernel32|d3d11|dxgi)\.dll$' || true)
[[ -z $bad ]] || { echo "d3d9.dll imports DLLs Play does not route: $bad" >&2; exit 1; }
rwx=$(llvm-readobj --sections "$out/d3d9.dll" \
      | awk '/Name: \./{n=$2;w=0;x=0} /IMAGE_SCN_MEM_WRITE/{w=1} /IMAGE_SCN_MEM_EXECUTE/{x=1} \
             /^ *\]$/{if(w&&x)print n}')
[[ -z $rwx ]] || { echo "d3d9.dll has writable code section(s): $rwx" >&2; exit 1; }
for name in Direct3DCreate9 Direct3DCreate9Ex D3DPERF_BeginEvent D3DPERF_EndEvent; do
    llvm-readobj --coff-exports "$out/d3d9.dll" | grep -q "Name: $name\$" \
        || { echo "d3d9.dll does not export $name" >&2; exit 1; }
done
# Play can only hand a fault inside this DLL to the guest thread's handler if the
# image carries unwind data: an empty exception directory means the OS cannot
# unwind out of it, so the fault is never dispatched and the launcher dies with
# no crash record. An empty table is a build failure, not a runtime surprise.
python3 -I "$here/check-unwind-data.py" "$out/d3d9.dll"
