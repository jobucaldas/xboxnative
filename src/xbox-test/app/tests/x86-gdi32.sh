#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device i686 GDI route test; no console or native GDI presenter required.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
if [[ -z $cc32 ]]; then cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true); fi
[[ -n $cc32 ]] || { echo "x86-gdi32: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-gdi32.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
"$cc32" -O1 -Wall -Wextra -Werror -Wno-cast-function-type -nostdlib -Wl,--entry,entry@0 -Wl,--subsystem,console \
  -o "$tmp/x86-gdi32.exe" "$here/x86-gdi32-guest.c" -lkernel32 -luser32 -lgdi32
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror "$here/x86-gdi32.cpp" -o "$tmp/x86-gdi32"
"$tmp/x86-gdi32" "$tmp/x86-gdi32.exe"
