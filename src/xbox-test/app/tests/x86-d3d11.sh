#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Audits the x86 D3D11 bridge tables and PE32 struct layouts under Wine.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)
here="$root/src/xbox-test/app/tests"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-d3d11.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
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
  "$here/x86-d3d11.cpp" -o "$tmp/x86-d3d11.exe" -lws2_32 -luser32 "${thread_lib_flags[@]}" -lkernel32
prefix=${PRUN_WINEPREFIX:-${WINEPREFIX:-$HOME/.cache/xbn-wine/x86-d3d11}}
mkdir -p "$prefix"
(
  cd "$tmp"
  WINEPREFIX="$prefix" PRUN_WINEPREFIX="$prefix" WINEDEBUG=-all \
    WINEDLLOVERRIDES='winedbg.exe=d' timeout -k 2 180 wine "$tmp/x86-d3d11.exe"
)
