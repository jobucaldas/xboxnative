#!/bin/bash
# Deterministic off-device event-feed regression, no console input injected.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static test.cpp -o "$work/keyboard-test.exe"
WINEDEBUG=-all wine "$work/keyboard-test.exe"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static text-test.cpp -o "$work/keyboard-text-test.exe"
WINEDEBUG=-all wine "$work/keyboard-text-test.exe"
