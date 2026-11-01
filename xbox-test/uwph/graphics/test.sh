#!/bin/bash
# Off-device regression; no console access and no native GPU required.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static test.cpp -o "$work/graphics-test.exe" -ldxgi -ldxguid
WINEDEBUG=-all wine "$work/graphics-test.exe"

x86_64-w64-mingw32-g++ -std=c++17 -O2 -static adapter-test.cpp -o "$work/adapter-test.exe" -ldxgi -ldxguid
WINEDEBUG=-all wine "$work/adapter-test.exe"

x86_64-w64-mingw32-g++ -std=c++17 -O2 -static output-test.cpp -o "$work/output-test.exe" -ldxgi -ldxguid
WINEDEBUG=-all wine "$work/output-test.exe"

x86_64-w64-mingw32-g++ -std=c++17 -O2 -static hwnd-flip-test.cpp -o "$work/hwnd-flip-test.exe" -ldxgi -ldxguid
WINEDEBUG=-all wine "$work/hwnd-flip-test.exe"
