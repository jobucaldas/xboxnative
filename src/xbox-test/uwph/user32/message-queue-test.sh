#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out/message-tests
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static message_queue_test.cpp -o out/message-tests/test.exe
WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/vis-wine} WINEDEBUG=-all timeout -k 2 30 wine out/message-tests/test.exe "$(pwd)/out/layout/uwph-u32c.dll"
