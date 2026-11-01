#!/bin/bash
# Host Wine only; run build.sh first. No console access.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -O2 -static message_box_test.cpp -o "$work/message-box-test.exe"
cp out/layout/uwph-u32c.dll "$work/"
WINEDEBUG=-all wine "$work/message-box-test.exe"
