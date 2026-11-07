#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run after build.sh. Host Wine only; never contacts the console.
# Pass --parity to run the same checks against Wine's own user32.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static create_size_test.cpp -o "$work/create-size-test.exe"
if [ "${1:-}" = --parity ]; then
  WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/vis-wine} WINEDEBUG=-all wine "$work/create-size-test.exe"
else
  cp out/layout/uwph-u32c.dll "$work/"
  WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/vis-wine} WINEDEBUG=-all wine "$work/create-size-test.exe" "$(winepath -w "$work/uwph-u32c.dll")"
fi
