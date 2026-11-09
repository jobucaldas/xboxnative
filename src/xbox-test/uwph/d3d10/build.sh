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
x86_64-w64-mingw32-g++ -c -O2 -std=c++17 -fno-exceptions -fno-rtti -fno-stack-protector \
    -fno-asynchronous-unwind-tables -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type \
    "$here/d3d10.cpp" -o "$work/d3d10.obj"
lld-link-19 /dll /noentry /nodefaultlib /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat \
    /def:"$here/d3d10.def" /implib:"$work/d3d10.lib" /out:"$out/d3d10.dll" \
    "$work/d3d10.obj" /libpath:"$mingw_lib" libkernel32.a libuuid.a libd3d11.a libdxgi.a
