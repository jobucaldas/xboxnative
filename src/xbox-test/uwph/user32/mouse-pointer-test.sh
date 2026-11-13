#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
out=${1:-out}; mkdir -p "$out/mouse-pointer-tests"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static mouse_pointer_test.cpp -o "$out/mouse-pointer-tests/test.exe"
WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/vis-wine} WINEDEBUG=-all timeout -k 2 30 wine "$out/mouse-pointer-tests/test.exe" "$(pwd)/$out/layout/uwph-u32c.dll"
