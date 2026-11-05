#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out/graphics-tests
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static tests/graphics-factory.cpp -o out/graphics-tests/factory.exe -ldxgi -ldxguid
WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/vis-wine} WINEDEBUG=-all timeout -k 2 30 wine out/graphics-tests/factory.exe
