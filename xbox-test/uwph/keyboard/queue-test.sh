#!/bin/bash
# Read-only cross-lane integration against an owner DLL with PostMessageW.
# Pass the DLL path explicitly; old record DLLs must fail, never skip checks.
set -euo pipefail
if [[ $# != 1 ]];then echo "usage: $0 /absolute/path/to/uwph-u32c.dll" >&2;exit 2;fi
records=$(realpath "$1")
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static queue-test.cpp -o "$work/keyboard-queue-test.exe" -luser32
WINEDEBUG=-all timeout -k 2 30 wine "$work/keyboard-queue-test.exe" "$records"
