#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run the pinned official i686 SDL2 DLL inside the x86 interpreter under Wine.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)
here="$root/src/xbox-test/app/tests"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-sdl2.XXXXXX")
if [[ ${KEEP_X86_SDL2:-0} == 1 ]]; then echo "x86-sdl2 artifacts: $tmp"; else trap 'rm -rf "$tmp"' EXIT; fi

cc32=${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}
if [[ -z $cc32 ]]; then cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true); fi
[[ -n $cc32 ]] || { echo "x86-sdl2: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
extra=()
for dir in /nix/store/*-mcfgthread-i686-w64-mingw32-*/lib; do [[ -d $dir ]] && extra+=("-L$dir"); done

bash "$root/src/tools/fetch-sdl.sh" "$tmp/sdl"
sdl="$tmp/sdl/SDL2"
[[ -f $sdl/lib/x86/SDL2.dll && -f $sdl/lib/x86/SDL2.lib ]] || {
  echo "x86-sdl2: pinned SDL2 i686 DLL or import library missing" >&2; exit 1;
}
init_mask=${SDL2_INIT_MASK:-0x6030}
"$cc32" -O1 -Wall -Wextra -Werror "${extra[@]}" -DSDL2TEST_INIT_MASK="$init_mask" \
  -I"$sdl/include" -L"$sdl/lib/x86" -Wl,--stack,0x300000 \
  -o "$tmp/x86-sdl2-guest.exe" "$here/x86-sdl2.c" -lSDL2

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
x86_64-w64-mingw32-g++ -std=c++17 -D_GLIBCXX_HAS_GTHREADS=0 "${thread_flags[@]}" \
  -O1 -mno-red-zone -Wall -Wextra -Wno-cast-function-type -Wno-misleading-indentation -static \
  "$here/x86-sdl2.cpp" -o "$tmp/x86-sdl2.exe" -lws2_32 -luser32 \
  "${thread_lib_flags[@]}" -lkernel32

prefix=${PRUN_WINEPREFIX:-${WINEPREFIX:-/tmp/xbn-x86-sdl2-wine}}
mkdir -p "$prefix"
set +e
(
  cd "$tmp"
  WINEPREFIX="$prefix" PRUN_WINEPREFIX="$prefix" WINEDEBUG=-all \
    WINEDLLOVERRIDES='winedbg.exe=d;d3d9=n' timeout -k 2 180 wine "$tmp/x86-sdl2.exe" \
      "$tmp/x86-sdl2-guest.exe" "$sdl/lib/x86/SDL2.dll" "$tmp/d3d9.dll"
)
rc=$?
set -e
[[ $rc -eq 0 ]] || { echo "x86-sdl2: run failed with exit $rc"; exit "$rc"; }
