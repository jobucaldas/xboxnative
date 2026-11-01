#!/bin/bash
# Off-device Wine only. Build the helper first; no console access.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -O2 -static map_char_test.cpp -o "$work/map-char-test.exe"
cp out/layout/uwph-u32c.dll "$work/"
WINEDEBUG=-all wine "$work/map-char-test.exe"
