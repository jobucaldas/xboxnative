#!/bin/bash
# Offline Wine oracle. Never contacts the console.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static test.cpp -o "$work/shell32-test.exe"
WINEDEBUG=-all wine "$work/shell32-test.exe"
