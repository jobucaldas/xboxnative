#!/bin/bash
# Off-device Wine oracle and deterministic WGI-backend regression.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static test.cpp -o "$work/xinput-test.exe"
WINEDEBUG=-all wine "$work/xinput-test.exe"
