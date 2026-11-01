#!/bin/bash
# Off-device no-raw-endpoint contract, Wine differential.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static test.cpp -o "$work/rawinput-test.exe" -luser32
WINEDEBUG=-all timeout -k 2 30 wine "$work/rawinput-test.exe"
