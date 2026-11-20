#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Verify PE32 loading of the i686 Wine-built VC++ 2015+ donors and the ucrtbase api-set redirect; off-device only.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
donors=${1:-$root/out/wine-donors/runtime/x86}
cc32=${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}
if [[ -z $cc32 ]]; then cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true); fi
[[ -n $cc32 ]] || { echo "x86-vc2015: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
for tool in llvm-dlltool c++; do command -v "$tool" >/dev/null || { echo "x86-vc2015: missing $tool" >&2; exit 1; }; done
for dll in ucrtbase vcruntime140 msvcp140; do
  [[ -f $donors/$dll.dll ]] || { echo "x86-vc2015: missing $donors/$dll.dll; run src/tools/build-wine-donors.sh" >&2; exit 2; }
done
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-vc2015.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
printf 'LIBRARY api-ms-win-crt-runtime-l1-1-0.dll\nEXPORTS\n_c_exit\n' >"$tmp/crt.def"
printf 'LIBRARY msvcp140.dll\nEXPORTS\n_Mtx_lock\n' >"$tmp/msvcp.def"
llvm-dlltool -m i386 -d "$tmp/crt.def" -l "$tmp/crt.a"
llvm-dlltool -m i386 -d "$tmp/msvcp.def" -l "$tmp/msvcp.a"
"$cc32" -O1 -ffreestanding -fno-stack-protector -fno-builtin -nostdlib \
  -Wl,--entry,_entry -Wl,--subsystem,console -o "$tmp/fixture.exe" \
  "$here/x86-vc2015-fixture.c" "$tmp/crt.a" "$tmp/msvcp.a" -lkernel32
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$here/x86-vc2015.cpp" -o "$tmp/x86-vc2015"
"$tmp/x86-vc2015" "$tmp/fixture.exe" "$donors"
