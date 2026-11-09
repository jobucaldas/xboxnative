#!/bin/bash
# Off-device only (Wine); touches neither the console nor the network.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static mcf-ntdll-test.cpp -o "$work/mcf-ntdll-test.exe"
WINEDEBUG=-all timeout -k 5s 30s wine "$work/mcf-ntdll-test.exe"
