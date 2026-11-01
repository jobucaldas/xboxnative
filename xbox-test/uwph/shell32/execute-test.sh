#!/bin/bash
# Off-device only. Never invokes the host ShellExecute export.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static execute_test.cpp -o "$work/execute-test.exe"
WINEDEBUG=-all wine "$work/execute-test.exe"
