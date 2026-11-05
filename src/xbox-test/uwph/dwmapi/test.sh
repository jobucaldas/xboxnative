#!/bin/bash
# Off-device Wine differential and guest-record host regression. No console use.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static test.cpp -o "$work/dwmapi-test.exe"
records=$(realpath ../../app/out/layout/uwph-u32c.dll)
WINEDEBUG=-all wine "$work/dwmapi-test.exe" "$(WINEDEBUG=-all winepath -w "$records")"
