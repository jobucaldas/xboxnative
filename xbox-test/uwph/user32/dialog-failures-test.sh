#!/bin/bash
# Run after build.sh. Host Wine only; never contacts the console.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -O2 -static dialog_failures_test.cpp -o "$work/dialog-failures-test.exe"
cp out/layout/uwph-u32c.dll "$work/"
WINEDEBUG=-all wine "$work/dialog-failures-test.exe"
