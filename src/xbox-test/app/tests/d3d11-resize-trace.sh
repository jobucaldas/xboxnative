#!/usr/bin/env bash
# Off-device trace-ring contract test; uses the same D3D11 SDK headers as the shim.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out/graphics-tests
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -Wall -Wextra tests/d3d11-resize-trace.cpp -o out/graphics-tests/d3d11-resize-trace.exe -ld3d11 -ldxgi -ldxguid
export WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/xbn-d3d11-resize-trace-wine}
mkdir -p "$WINEPREFIX"
if [[ ! -f "$WINEPREFIX/system.reg" ]];then timeout -k 2 60 wineboot -u;fi
WINEDEBUG=-all timeout -k 2 30 wine out/graphics-tests/d3d11-resize-trace.exe
