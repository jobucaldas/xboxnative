#!/bin/bash
# Off-device only. Host Wine receives only the NULL request.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static file_operation_test.cpp -o "$work/file-operation-test.exe"
WINEDEBUG=-all wine "$work/file-operation-test.exe"
