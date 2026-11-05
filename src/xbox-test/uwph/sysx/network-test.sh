#!/bin/bash
# Off-device only. Native IPv4 TCP loopback, never AF_UNIX.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static network-test.cpp -lws2_32 -o "$work/network-test.exe"
result=0
WINEDEBUG=-all timeout 30s wine "$work/network-test.exe" > "$work/wine.log" 2>&1 || result=$?
cat "$work/wine.log"
(( result == 0 )) || exit "$result"
if grep -Eq 'wine: Unhandled (page fault|exception)' "$work/wine.log"; then
    echo 'FAIL: Wine reported an unhandled fault despite exit 0' >&2
    exit 1
fi
