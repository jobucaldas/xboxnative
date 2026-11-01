#!/bin/bash
# Run after build.sh. Wine is off-device; no console access.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -O2 -static ime_association_test.cpp -limm32 -o "$work/ime-association-test.exe"
cp out/layout/uwph-u32c.dll "$work/"
WINEDEBUG=-all wine "$work/ime-association-test.exe"
