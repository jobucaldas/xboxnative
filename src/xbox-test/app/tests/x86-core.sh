#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the owned i686 fixture, runs the x86 execution core tests, and (with
# --wine) cross-checks the same fixture under real Windows semantics (Wine):
# identical stdout and exit code. Deterministic and console-free.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
if [[ -z $cc32 ]]; then
  # NixOS: the i686 mingw gcc wrapper lives in the store but is not on PATH.
  cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true)
fi
[[ -n $cc32 ]] || { echo "x86-core: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
tmp=$(mktemp -d /tmp/x86-core.XXXXXX); trap 'rm -rf "$tmp"' EXIT
"$cc32" -O1 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables \
  -mno-sse -mno-mmx -mno-80387 -nostdlib -Wl,--entry,_fixture_start@0 -Wl,--subsystem,console -Wl,--dynamicbase \
  -Wl,--no-seh -Wl,--image-base,0x400000 -o "$tmp/fixture.exe" "$here/x86-fixture/fixture.c" -lkernel32 -lmsvcrt
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror "$here/x86-core.cpp" -o "$tmp/x86-core"
"$tmp/x86-core" "$tmp/fixture.exe"
if [[ ${1:-} == --wine ]]; then
  set +e; out=$(WINEDEBUG=-all wine "$tmp/fixture.exe" | tr -d '\r'); rc=$?; set -e
  [[ $rc -eq 42 ]] || { echo "X86-CORE FAIL: wine exit $rc (want 42)"; exit 1; }
  want=$'x86-fixture: start\nx86-fixture: teb/peb ok\ncrc=1632946162\nhello from 32-bit\nchecks=15\nfailed=0'
  [[ $out == "$want" ]] || { echo "X86-CORE FAIL: wine stdout differs"; echo "$out"; exit 1; }
  echo "X86-CORE PASS: fixture stdout and exit code identical under Wine (real kernel32/msvcrt)"
fi
