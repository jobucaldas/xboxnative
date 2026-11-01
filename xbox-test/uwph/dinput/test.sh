#!/bin/bash
# Off-device invalid-request differential and honest unsupported creation.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static test.cpp -o "$work/dinput-test.exe" -ldxguid
WINEDEBUG=-all timeout -k 2 30 wine "$work/dinput-test.exe"
