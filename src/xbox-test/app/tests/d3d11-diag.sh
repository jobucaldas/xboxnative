#!/usr/bin/env bash
# Off-device (Wine) contracts of host/d3d11_diag.h on a native d3d11 device.
# Two runs: the failed-call log, then the draw summary. Separate processes
# because Wine 11.0 faults at teardown when one process both logs failures and
# draws on a real device.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out/graphics-tests
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -Wall -Wextra tests/d3d11-diag.cpp -o out/graphics-tests/d3d11-diag.exe -ld3d11 -ldxgi -ldxguid
WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/vis-wine} WINEDEBUG=-all timeout -k 2 60 wine out/graphics-tests/d3d11-diag.exe
WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/vis-wine} WINEDEBUG=-all timeout -k 2 60 wine out/graphics-tests/d3d11-diag.exe draw
