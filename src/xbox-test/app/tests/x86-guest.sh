#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs a real i686 program built against the mingw CRT (PE32) through the
# interpreter and the guest OS surface (host/x86/x86_guest.h): CRT startup,
# printf's own FP formatter, malloc/qsort, TLS, the heap, VirtualAlloc and the
# kernel32 calls a game's startup makes. With --wine the same binary runs under
# real Windows semantics and stdout and the exit code must be identical.
# Deterministic and console-free.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
if [[ -z $cc32 ]]; then
  # NixOS: the i686 mingw gcc wrapper lives in the store but is not on PATH.
  cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true)
fi
[[ -n $cc32 ]] || { echo "x86-guest: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
# NixOS's wrapper links its mcfgthread runtime from a separate store path.
extra=()
for d in /nix/store/*-mcfgthread-i686-w64-mingw32-*/lib; do [[ -d $d ]] && extra+=("-L$d"); done
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-guest.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
# The fixture carries an application manifest declaring Windows 10 support, so
# GetVersionEx reports 10.0 under real Windows/Wine as it does on the console.
rc32="$(dirname "$cc32")/i686-w64-mingw32-windres"
[[ -x $rc32 ]] || { echo "x86-guest: no i686-w64-mingw32-windres" >&2; exit 1; }
# The .rc is preprocessed, and the wrapper's preprocessor is the cross gcc.
PATH="$(dirname "$cc32"):$PATH" "$rc32" -O coff -I "$here/x86-guest" -i "$here/x86-guest/fixture-crt.rc" -o "$tmp/fixture-crt.res"
"$cc32" -O1 -Wall -Wextra -Werror "${extra[@]}" -o "$tmp/fixture-crt.exe" "$here/x86-guest/fixture-crt.c" "$tmp/fixture-crt.res"
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror "$here/x86-guest.cpp" -o "$tmp/x86-guest"
set +e
out=$("$tmp/x86-guest" "$tmp/fixture-crt.exe"); rc=$?
set -e
grep -q '^guest: base=0x00400000 ' <<<"$out" || { echo "X86-GUEST FAIL: no PE32 image loaded"; echo "$out"; exit 1; }
body=$(sed '/^guest: /d' <<<"$out")
[[ $rc -eq 42 ]] || { echo "X86-GUEST FAIL: exit $rc (want 42)"; echo "$out"; exit 1; }
want='x86-crt: start
x86-crt: stdio ok
x86-crt: printf -42|42|beef|BEEF|10|Z|str| 3.14|-1234567|78187493530
x86-crt: sprintf 7/hi/1.5 abcde
x86-crt: heap ok
x86-crt: strings ok
x86-crt: varargs 60 4.00
x86-crt: kernel32 ok
x86-crt: sync ok
x86-crt: checks 35 failed 0'
[[ $body == "$want" ]] || { echo "X86-GUEST FAIL: stdout differs"; diff <(echo "$want") <(echo "$body") || true; exit 1; }
echo "X86-GUEST PASS: mingw CRT program (printf with doubles, heap, qsort, TLS, VirtualAlloc) ran in the interpreter to exit 42"
# The launcher's own PE32 entry point (host/x86/x86_play.h): the structured
# result launch.h logs and launch_result.json is built from, plus the live
# output tee and the pre-entry progress callback.
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror "$here/x86-play.cpp" -o "$tmp/x86-play"
set +e
pout=$("$tmp/x86-play" "$tmp/fixture-crt.exe"); prc=$?
set -e
[[ $prc -eq 0 ]] || { echo "X86-GUEST FAIL: x86_play exit $prc"; echo "$pout"; exit 1; }
grep -q '^play: loaded=1 finished=1 exit=42 .* imports=65 bound=65 ' <<<"$pout" || { echo "X86-GUEST FAIL: x86_play result"; echo "$pout"; exit 1; }
grep -q '^play: on_loaded calls=1 before-entry=1 ' <<<"$pout" || { echo "X86-GUEST FAIL: x86_play progress callback"; echo "$pout"; exit 1; }
! grep -q '^play: unresolved\|^play: fault' <<<"$pout" || { echo "X86-GUEST FAIL: x86_play reported a fault or unresolved import"; echo "$pout"; exit 1; }
pbody=$(sed '/^play: /d' <<<"$pout")
[[ $pbody == "$want" ]] || { echo "X86-GUEST FAIL: x86_play tee differs"; diff <(echo "$want") <(echo "$pbody") || true; exit 1; }
echo "X86-GUEST PASS: the launcher's PE32 entry point reports the same run (loaded, imports bound, exit 42) and streams its output"
if [[ ${1:-} == --wine ]]; then
  set +e; wout=$(WINEDEBUG=-all wine "$tmp/fixture-crt.exe" | tr -d '\r'); wrc=$?; set -e
  [[ $wrc -eq 42 ]] || { echo "X86-GUEST FAIL: wine exit $wrc (want 42)"; exit 1; }
  [[ $wout == "$want" ]] || { echo "X86-GUEST FAIL: wine stdout differs"; echo "$wout"; exit 1; }
  echo "X86-GUEST PASS: same binary, stdout and exit code identical under Wine (real kernel32/msvcrt)"
fi
