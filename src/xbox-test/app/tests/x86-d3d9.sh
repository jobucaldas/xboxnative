#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs an i686 guest inside the interpreter against the real x64 shim under Wine.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)
here="$root/src/xbox-test/app/tests"
donors=${1:-$root/out/wine-donors/runtime/x86}
[[ -f $donors/d3dx9_43.dll ]] || { echo "x86-d3d9: missing d3dx9_43.dll donor; build src/tools/build-wine-donors.sh first" >&2; exit 1; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-d3d9.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cc32=${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}
if [[ -z $cc32 ]]; then cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true); fi
[[ -n $cc32 ]] || { echo "x86-d3d9: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
"$cc32" -O1 -Wall -Wextra -Werror -nostdlib -Wl,--entry,entry@0 -Wl,--subsystem,console \
  -o "$tmp/x86-d3d9-guest.exe" "$here/x86-d3d9-guest.c" -ld3d9 -lkernel32
bash "$root/src/xbox-test/uwph/d3d9/build.sh" "$tmp"
bash "$root/src/tools/build-d3d9-translator.sh" "$tmp"
mcfgthread_include=
for candidate in /nix/store/*-mcfgthread-x86_64-w64-mingw32-*-dev/include; do
  if [[ -f "$candidate/mcfgthread/gthr.h" ]]; then mcfgthread_include=$candidate; break; fi
done
thread_flags=()
thread_lib_flags=()
[[ -z $mcfgthread_include ]] || thread_flags=(-isystem "$mcfgthread_include")
for candidate in /nix/store/*-mcfgthread-x86_64-w64-mingw32-*/lib/libmcfgthread.a; do
  if [[ -f "$candidate" ]]; then thread_lib_flags=(-L "$(dirname "$candidate")" -lmcfgthread); break; fi
done
x86_64-w64-mingw32-g++ -std=c++17 -D_GLIBCXX_HAS_GTHREADS=0 "${thread_flags[@]}" -O1 -mno-red-zone -Wall -Wextra -Wno-cast-function-type -Wno-misleading-indentation -static \
  "$here/x86-d3d9.cpp" -o "$tmp/x86-d3d9.exe" -lws2_32 -luser32 "${thread_lib_flags[@]}" -lkernel32
prefix=${PRUN_WINEPREFIX:-${WINEPREFIX:-/tmp/xbn-x86-d3d9-bridge-wine}}
mkdir -p "$prefix"
(
  cd "$tmp"
  WINEPREFIX="$prefix" PRUN_WINEPREFIX="$prefix" WINEDEBUG=-all \
    WINEDLLOVERRIDES='winedbg.exe=d;d3d9=n' timeout -k 2 180 wine "$tmp/x86-d3d9.exe" \
      "$tmp/x86-d3d9-guest.exe" "$tmp/d3d9.dll" "$donors"
)
