#!/bin/bash
# Off-device event regression; no console or physical pointer claim.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static test.cpp -o "$work/pointer-test.exe"
WINEDEBUG=-all wine "$work/pointer-test.exe"
