#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device proof of Play's OpenGL loader path with an owned SDL3 program (official SDL3
# release DLL, src/tools/fetch-sdl.sh): LoadLibrary("OPENGL32.DLL") must succeed with
# Play's own module (the console has none). Window/context creation is reported: it needs
# an OpenGL driver (research/OPENGL-PLAN.md), so today it prints BLOCKED, never PASS; any
# other failure fails the script. No console or game claim.
set -euo pipefail
cd "$(dirname "$0")/.."
export WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/prun-wine}
export WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d
bash ../../tools/fetch-sdl.sh out/sdl
bash tests/play-runtime.sh >/dev/null   # builds the Play runner; its own gates must pass
out=out/play-tests
mkdir -p "$out/donors"; cp out/layout/donors/*.dll "$out/donors/"
CB=${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}
crt=${XWIN_ROOT:-$HOME/.cache/uwp-crossbuild/xwin}
game=out/play-sdl/sdl3gl; rm -rf "$game" "$out/obj/sdl3gl"; mkdir -p "$game"
UWP_OBJ_DIR="$out/obj/sdl3gl" "$CB/scripts/build.sh" --out "$game/play-sdl3-gl.exe" --link-arg "out/sdl/SDL3/lib/x64/SDL3.lib" tests/play-sdl3-gl.c -- /MT /O2 "/Iout/sdl/SDL3/include" >/dev/null
lld-link-19 "$out"/obj/sdl3gl/*.obj /subsystem:console /out:"$game/play-sdl3-gl.exe" \
 /libpath:"$crt/crt/lib/x86_64" /libpath:"$crt/sdk/lib/um/x86_64" /libpath:"$crt/sdk/lib/ucrt/x86_64" \
 kernel32.lib "out/sdl/SDL3/lib/x64/SDL3.lib"
cp "out/sdl/SDL3/lib/x64/SDL3.dll" "$game/"
log=$out/play-sdl3-gl.log
set +e
PRUN_GAME_ROOT=$(winepath -w "$game") PRUN_GAME_EXE=play-sdl3-gl.exe PRUN_GAME_SECONDS=${PRUN_GAME_SECONDS:-20} \
 timeout -k 2 60 wine "$out/play-runner.exe" game > "$log" 2>&1
set -e
grep -q "PLAY-SDL3-GL STAGE gl-library-loaded" "$log" || {
    echo "PLAY-SDL3-GL FAIL (see $log)"; grep -E "PLAY-SDL3-GL|LoadLibrary failed|unhandled" "$log" | head -5; exit 1; }
if grep -q "PLAY-SDL3-GL PASS" "$log"; then grep "PLAY-SDL3-GL" "$log"; echo "PLAY-SDL3-GL context PASS"
elif grep -q "PLAY-SDL3-GL BLOCKED" "$log"; then grep "PLAY-SDL3-GL" "$log"; echo "PLAY-SDL3-GL library-load PASS; window/context BLOCKED (no OpenGL driver yet)"
else echo "PLAY-SDL3-GL FAIL (see $log)"; grep "PLAY-SDL3-GL" "$log"; exit 1; fi
