#!/bin/bash
# Off-device fixed US layout and Win32 buffer-contract differential.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static names-test.cpp -o "$work/keyboard-names-test.exe" -luser32
env -u DISPLAY -u WAYLAND_DISPLAY WINEDEBUG=-all wine "$work/keyboard-names-test.exe"
