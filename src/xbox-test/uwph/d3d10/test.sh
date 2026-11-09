#!/bin/bash
# Off-device Wine regression for Play's D3D10 shim; never contacts the console.
# Builds the shim, then drives its entry points against Wine's d3d11.dll.
set -euo pipefail
cd "$(dirname "$0")"
out=${1:-/tmp/xbn-d3d10-test}
rm -rf "$out";mkdir -p "$out"
bash build.sh "$out"
# The test links no CRT either: mingw's g++ cannot link on this machine, and a
# shim test should not depend on a C runtime the console's guests do not have.
# -fno-builtin -fno-tree-loop-distribute-patterns for the same reason as the
# shim: its own memset must not become a call to itself.
mingw_lib=$(dirname "$(x86_64-w64-mingw32-g++ -print-file-name=libkernel32.a)")
x86_64-w64-mingw32-g++ -c -O2 -std=c++17 -fno-builtin -fno-tree-loop-distribute-patterns \
    -fno-exceptions -fno-rtti -fno-stack-protector -fno-asynchronous-unwind-tables \
    test.cpp -o "$out/test.obj"
lld-link-19 /nodefaultlib /entry:d3d10_test_entry /subsystem:console,6.02 /dynamicbase /nxcompat \
    /out:"$out/d3d10-test.exe" "$out/test.obj" /libpath:"$mingw_lib" \
    libkernel32.a libuser32.a libuuid.a libd3d11.a libdxgi.a
# Wine recognises a DLL by name and serves Wine's own builtin export table for
# it, so the shim's d3d10_1-only names would be hidden. Play's loader reads the
# file's export table directly and has no such notion; force native loading here
# so the test exercises the same table Play will.
WINEDEBUG=-all WINEDLLOVERRIDES='winedbg.exe=d;d3d10,d3d10_1,d3d10core=n' timeout -k 2 60 wine "$out/d3d10-test.exe"
