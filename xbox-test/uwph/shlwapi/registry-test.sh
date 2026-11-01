#!/bin/bash
# Off-device only. Never calls the host registry deletion export.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static registry_test.cpp -o "$work/registry-test.exe"
WINEDEBUG=-all wine "$work/registry-test.exe"
