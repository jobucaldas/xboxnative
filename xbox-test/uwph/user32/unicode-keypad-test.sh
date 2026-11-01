#!/bin/bash
# Off-device Wine only. Build the helper first; no console access.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -O2 -static unicode_keypad_test.cpp -o "$work/unicode-keypad-test.exe"
cp out/layout/uwph-u32c.dll "$work/"
WINEDEBUG=-all wine "$work/unicode-keypad-test.exe"
