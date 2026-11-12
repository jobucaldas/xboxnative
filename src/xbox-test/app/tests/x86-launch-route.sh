#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The launcher's PE32 route end to end, off-device and console-free: build the
# owned i686 fixture, then run it exactly as launch.h's lp_run_pe32 does (the
# same entry point, x86::run, with the game-sized cap) and map the result
# through x86_route::report. Asserts exit code 42 and the fixture's exact
# stdout, and that the route reports success with its PRUN EVENT line.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
if [[ -z $cc32 ]]; then
  # NixOS: the i686 mingw gcc wrapper lives in the store but is not on PATH.
  cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true)
fi
[[ -n $cc32 ]] || { echo "x86-launch-route: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-launch-route.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
"$cc32" -O1 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables \
  -mno-sse -mno-mmx -mno-80387 -nostdlib -Wl,--entry,_fixture_start@0 -Wl,--subsystem,console -Wl,--dynamicbase \
  -Wl,--no-seh -Wl,--image-base,0x400000 -o "$tmp/fixture.exe" "$here/x86-fixture/fixture.c" -lkernel32 -lmsvcrt
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror "$here/x86-launch-route.cpp" -o "$tmp/x86-launch-route"
"$tmp/x86-launch-route" "$tmp/fixture.exe"
