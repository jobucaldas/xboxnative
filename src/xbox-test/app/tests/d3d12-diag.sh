#!/usr/bin/env bash
# Off-device (Wine) contracts of host/d3d12_diag.h on a native d3d12 device.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out/graphics-tests
# A NixOS mingw gcc keeps libstdc++'s gthread behind mcfgthread; point at the
# dev output when the environment names it (a distro mingw needs neither).
extra=()
[[ -n ${MCFG_INC:-} ]] && extra+=(-I"$MCFG_INC")
[[ -n ${MCFG_LIB:-} ]] && extra+=(-L"$MCFG_LIB")
x86_64-w64-mingw32-g++ -std=c++17 -fsyntax-only -Wall -Wextra tests/d3d12-vtbl-layout.cpp
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -Wall -Wextra "${extra[@]}" tests/d3d12-diag.cpp -o out/graphics-tests/d3d12-diag.exe -ld3d12 -ldxgi -ldxguid
WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/vis-wine} WINEDEBUG=-all timeout -k 2 60 wine out/graphics-tests/d3d12-diag.exe
WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/vis-wine} WINEDEBUG=-all timeout -k 2 60 wine out/graphics-tests/d3d12-diag.exe trace
