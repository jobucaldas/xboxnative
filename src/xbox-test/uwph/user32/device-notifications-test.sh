#!/bin/bash
# Run after build.sh. Host Wine only; never contacts the console.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -O2 -static device_notifications_test.cpp -o "$work/device-notifications-test.exe"
cp out/layout/uwph-u32c.dll "$work/"
WINEDEBUG=-all wine "$work/device-notifications-test.exe"
