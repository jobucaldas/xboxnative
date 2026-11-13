#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Compare real PE32 MinGW CRT header layouts in legacy msvcrt and UCRT modes.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
if [[ -z $cc32 ]]; then
  cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true)
fi
[[ -n $cc32 ]] || { echo "x86-apiset-layout: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
wine="${WINE:-$(command -v wine || command -v wine64 || true)}"
[[ -n $wine ]] || { echo "x86-apiset-layout: Wine is required to run the layout probes" >&2; exit 1; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-apiset-layout.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
common=(-x c -std=c11 -O1 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -mno-sse -mno-mmx -mno-80387 -nostdlib -Wl,--entry,_layout_start@0 -Wl,--subsystem,console -Wl,--no-seh)
"$cc32" "${common[@]}" -D__MSVCRT_VERSION__=0x0700 -D_WIN32_WINNT=0x0600 -D_USE_32BIT_TIME_T -U_UCRT \
  -o "$tmp/msvcrt.exe" "$here/x86-apiset-layout.cpp" -lkernel32
"$cc32" "${common[@]}" -D_UCRT -U__MSVCRT_VERSION__ \
  -o "$tmp/ucrt.exe" "$here/x86-apiset-layout.cpp" -lkernel32
WINEPREFIX="$tmp/wineprefix" WINEDEBUG=-all "$wine" "$tmp/msvcrt.exe" > "$tmp/msvcrt.txt" 2>/dev/null
WINEPREFIX="$tmp/wineprefix" WINEDEBUG=-all "$wine" "$tmp/ucrt.exe" > "$tmp/ucrt.txt" 2>/dev/null
python3 -I - "$tmp/msvcrt.txt" "$tmp/ucrt.txt" <<'PY'
import sys

def read(path):
    result = {}
    with open(path, encoding="ascii") as f:
        for line in f:
            key, value = line.rstrip("\n").split("=", 1)
            result[key] = int(value)
    return result

old, ucrt = map(read, sys.argv[1:])
keys = sorted(set(old) | set(ucrt))
names = {key.split(".", 1)[0] for key in keys if old.get(key) != ucrt.get(key)}
expected = {"FILE", "_finddata_t", "_finddatai64_t", "_timeb", "_wfinddata_t", "_wfinddatai64_t", "lconv", "stat"}
for name in sorted(names):
    print(f"layout mismatch: {name}")
    for key in keys:
        if key.startswith(name + ".") and old.get(key) != ucrt.get(key):
            print(f"  {key}: msvcrt={old.get(key, 'absent')} UCRT={ucrt.get(key, 'absent')}")
if names != expected:
    print(f"FAIL unexpected layout mismatch set: expected {sorted(expected)}, got {sorted(names)}")
    sys.exit(1)
print(f"x86-apiset-layout: {len(names)} reviewed layout mismatches; all other measured layouts match")
PY
