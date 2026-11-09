#!/bin/bash
# Run after build.sh. Host Wine only; never contacts the console.
# The DPI process/thread context is first-choice-wins and irreversible, so the
# same fixed sequence runs in two fresh processes and the transcripts must match.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -O2 -static dpi_context_test.cpp -o "$work/dpi-context-test.exe"
cp out/layout/uwph-u32c.dll "$work/"
export WINEDEBUG=-all
wine "$work/dpi-context-test.exe" host > "$work/host.txt"
wine "$work/dpi-context-test.exe" dll > "$work/dll.txt"
# The last line only names the side that produced it.
sed -i '$d' "$work/host.txt" "$work/dll.txt"
if ! diff -u "$work/host.txt" "$work/dll.txt"; then
 echo "FAIL: DPI context transcript differs from host Wine user32" >&2
 exit 1
fi
printf 'PASS: DPI context transcript matches host Wine user32 (%s lines)\n' "$(wc -l < "$work/host.txt")"
