#!/bin/bash
# Run after build.sh; all execution is off-device under Wine.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -O2 -static keyboard_test.cpp -o "$work/keyboard-test.exe"
cp out/layout/uwph-u32c.dll "$work/"
WINEDEBUG=-all wine "$work/keyboard-test.exe"
