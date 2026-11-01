#!/bin/bash
# Read-only integration with an explicitly supplied owner DLL. Missing exports fail.
set -euo pipefail
[[ $# == 1 ]] || { echo "usage: $0 /absolute/path/to/uwph-u32c.dll" >&2; exit 2; }
records=$(realpath "$1")
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static queue-test.cpp -o "$work/pointer-queue-test.exe" -luser32
WINEDEBUG=-all timeout -k 2 30 wine "$work/pointer-queue-test.exe" "$records"
