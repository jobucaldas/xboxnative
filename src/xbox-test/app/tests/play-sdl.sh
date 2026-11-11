#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device Wine proof: owned SDL3 and SDL2 programs, linked against the official
# SDL release DLLs (src/tools/fetch-sdl.sh), run through the same Play runtime as
# launcher Play. Each must create a window and a Direct3D 11 renderer, present
# frames and shut down (exit 42). No console or game claim.
set -euo pipefail
cd "$(dirname "$0")/.."
export WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/prun-wine}
export WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d
bash ../../tools/fetch-sdl.sh out/sdl
bash tests/play-runtime.sh >/dev/null   # builds the Play runner; its own gates must pass
out=out/play-tests
mkdir -p "$out/donors"; cp out/layout/donors/*.dll "$out/donors/"
CB=${UWP_CROSSBUILD:-out/uwp-crossbuild}
crt=${XWIN_ROOT:-$HOME/.cache/uwp-crossbuild/xwin}
status=0
for v in 3 2;do
 game=out/play-sdl/sdl$v; rm -rf "$game" "$out/obj/sdl$v"; mkdir -p "$game"
 inc=out/sdl/SDL$v/include; [[ $v == 2 ]] || inc=out/sdl/SDL3/include
 UWP_OBJ_DIR="$out/obj/sdl$v" "$CB/scripts/build.sh" --out "$game/play-sdl$v.exe" --link-arg "out/sdl/SDL$v/lib/x64/SDL$v.lib" tests/play-sdl$v.c -- /MT /O2 "/I$inc" >/dev/null
 # Relink against desktop import names, as the owned C++ fixtures do.
 lld-link-19 "$out"/obj/sdl$v/*.obj /subsystem:console /out:"$game/play-sdl$v.exe" \
  /libpath:"$crt/crt/lib/x86_64" /libpath:"$crt/sdk/lib/um/x86_64" /libpath:"$crt/sdk/lib/ucrt/x86_64" \
  kernel32.lib "out/sdl/SDL$v/lib/x64/SDL$v.lib"
 cp "out/sdl/SDL$v/lib/x64/SDL$v.dll" "$game/"
 log=$out/play-sdl$v.log
 set +e
 PRUN_GAME_ROOT=$(winepath -w "$game") PRUN_GAME_EXE=play-sdl$v.exe PRUN_GAME_SECONDS=${PRUN_GAME_SECONDS:-20} \
  timeout -k 2 60 wine "$out/play-runner.exe" game > "$log" 2>&1
 set -e
 if grep -q "PLAY-TEST executed=1 finished=1 success=1 code=42" "$log" && grep -q "PLAY-SDL$v PASS" "$log";then grep "PLAY-SDL$v PASS" "$log"
 else echo "PLAY-SDL$v FAIL (see $log)"; grep -E "PLAY-SDL$v FAIL|EXE returned|unhandled|missing import hit|mapping failed" "$log" | head -5 || true; status=1;fi
done
exit $status
