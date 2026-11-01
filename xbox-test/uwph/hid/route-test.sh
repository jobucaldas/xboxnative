#!/bin/bash
# Production route regression. Existing isolated HID smoke remains unchanged.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static route-test.cpp -o "$work/hid-route-test.exe"
WINEDEBUG=-all wine "$work/hid-route-test.exe"
