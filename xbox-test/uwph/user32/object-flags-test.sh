#!/bin/bash
# Off-device Wine only; build user32 DLL first. No console access.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -O2 -static object_flags_test.cpp -o "$work/object-flags-test.exe"
cp out/layout/uwph-u32c.dll "$work/"
WINEDEBUG=-all wine "$work/object-flags-test.exe"
