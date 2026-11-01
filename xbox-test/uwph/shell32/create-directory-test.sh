#!/bin/bash
# Off-device only. Host shell calls receive relative paths exclusively.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static create_directory_test.cpp -o "$work/create-directory-test.exe"
WINEDEBUG=-all wine "$work/create-directory-test.exe"
