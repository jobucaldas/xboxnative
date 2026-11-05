#!/bin/bash
# Off-device Wine regression; never contacts or modifies the console.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -O2 -static test.cpp -o "$work/gdi32-test.exe"
WINEDEBUG=-all wine "$work/gdi32-test.exe"
