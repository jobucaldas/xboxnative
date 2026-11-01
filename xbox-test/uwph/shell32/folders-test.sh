#!/bin/bash
# Off-device only. Never invokes host shell folder discovery.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static folders_test.cpp -o "$work/folders-test.exe"
WINEDEBUG=-all wine "$work/folders-test.exe"
