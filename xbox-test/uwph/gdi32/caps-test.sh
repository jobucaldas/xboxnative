#!/bin/bash
# Run after ../user32/build.sh. Off-device only; never touches the console.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -O2 -static caps_test.cpp -lgdi32 -o "$work/caps-test.exe"
cp ../user32/out/layout/uwph-u32c.dll "$work/"
WINEDEBUG=-all wine "$work/caps-test.exe"
