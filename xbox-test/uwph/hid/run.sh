#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
bash "$here/build.sh"
cd "$here/out"
output=$(wine hid-test.exe 2>/dev/null | tr -d '\r')
printf '%s\n' "$output"
grep -Fx 'HID-STUB-OK symbols=20 checks=23' <<< "$output"
test "$(grep -c '^HID CHECK .* PASS$' <<< "$output")" -eq 23
