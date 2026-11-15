#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Verify production PE32 loading of an i686 Wine-built VC110 CRT donor; off-device only.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
donors=${1:-$root/out/wine-donors/runtime/x86}
cc32=${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}
if [[ -z $cc32 ]]; then cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true); fi
[[ -n $cc32 ]] || { echo "x86-vc-crt: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
for tool in llvm-dlltool c++; do command -v "$tool" >/dev/null || { echo "x86-vc-crt: missing $tool" >&2; exit 1; }; done
[[ -f $donors/msvcr110.dll ]] || { echo "x86-vc-crt: missing $donors/msvcr110.dll; run src/tools/build-wine-donors.sh" >&2; exit 2; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-vc-crt.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
printf 'LIBRARY msvcr110.dll\nEXPORTS\n strlen\n malloc\n free\n' >"$tmp/msvcr110.def"
llvm-dlltool -m i386 -d "$tmp/msvcr110.def" -l "$tmp/msvcr110.a"
"$cc32" -O1 -ffreestanding -fno-stack-protector -fno-builtin -nostdlib \
  -Wl,--entry,_entry -Wl,--subsystem,console -o "$tmp/fixture.exe" \
  "$here/x86-vc-crt-fixture.c" "$tmp/msvcr110.a" -lkernel32
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$here/x86-vc-crt.cpp" -o "$tmp/x86-vc-crt"
"$tmp/x86-vc-crt" "$tmp/fixture.exe" "$donors"
