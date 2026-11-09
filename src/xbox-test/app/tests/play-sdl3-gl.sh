#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device proof of Play's OpenGL loader path with an owned SDL3 program (official SDL3
# release DLL, src/tools/fetch-sdl.sh): LoadLibrary("OPENGL32.DLL") must succeed with
# Play's own module (the console has none) or, when the runtime directory carries it, Mesa's
# d3d12 driver (research/OPENGL-PLAN.md). The program then creates a window and context,
# clears and presents frames. A stage the host cannot run (Wine without a working d3d12 backend)
# prints BLOCKED with the reason, never PASS; any other failure fails the script. No console or game claim.
# Wine 11's d3d12 does run the driver (it answers ID3D12Device9 but not CreateCommandQueue1, which
# patches/mesa-src/0002 covers), so the d3d12 pass is expected to present a frame here too.
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
# One run of the guest. $1 = label, $2 = GALLIUM_DRIVER value ("" leaves it unset: the patched default, d3d12).
run_pass() {
    log=$out/play-sdl3-gl-$1.log
    set +e
    if [ -n "$2" ]; then export GALLIUM_DRIVER=$2; else unset GALLIUM_DRIVER; fi
    PRUN_GAME_ROOT=$(winepath -w "$game") PRUN_GAME_EXE=play-sdl3-gl.exe PRUN_GAME_SECONDS=${PRUN_GAME_SECONDS:-20} \
     timeout -k 2 60 wine "$out/play-runner.exe" game > "$log" 2>&1
    set -e
    grep -q "PLAY-SDL3-GL STAGE gl-library-loaded" "$log" || {
        echo "PLAY-SDL3-GL FAIL [$1] (see $log)"; grep -E "PLAY-SDL3-GL|LoadLibrary failed|unhandled" "$log" | head -5; exit 1; }
    grep "PLAY-SDL3-GL" "$log" | sed "s/^/[$1] /"
    if grep -q "PLAY-SDL3-GL PASS frame-presented" "$log"; then result=PASS
    elif grep -q "PLAY-SDL3-GL BLOCKED" "$log"; then result=BLOCKED
    else echo "PLAY-SDL3-GL FAIL [$1] (see $log)"; exit 1; fi
}
mesa=0; [ -f out/play-tests/runtime/opengl32.dll ] && mesa=1
# Pass 1, Mesa's default: the d3d12 driver. It needs a D3D12 backend that reports DISPLAY-capable formats (the
# console's; Wine's vkd3d does too since patch 0002). PRUN_REQUIRE_D3D12=1 turns BLOCKED into a failure for hosts
# that should run it.
run_pass d3d12 ""
if [ "$result" = PASS ] && grep -q 'renderer=".*D3D12' "$log"; then echo "PLAY-SDL3-GL d3d12 context+frame PASS"
elif [ "$result" = PASS ]; then echo "PLAY-SDL3-GL FAIL: frame presented but the renderer is not d3d12"; exit 1
else
    echo "PLAY-SDL3-GL d3d12 BLOCKED on this host (reason above); not a pass"
    [ "${PRUN_REQUIRE_D3D12:-0}" = 1 ] && { echo "PLAY-SDL3-GL FAIL: PRUN_REQUIRE_D3D12=1"; exit 1; }
fi
# Pass 2, Mesa's CPU rasterizer (GALLIUM_DRIVER=softpipe): d3d12 is not involved, but the loader, gdi32 forwarding,
# pixel formats, WGL context, glClear and SwapBuffers are, so this is the off-device gate for everything but D3D12.
if [ "$mesa" = 1 ]; then
    run_pass softpipe softpipe
    [ "$result" = PASS ] && grep -q 'renderer="softpipe"' "$log" || { echo "PLAY-SDL3-GL FAIL [softpipe]: expected a presented frame from Mesa softpipe"; exit 1; }
    echo "PLAY-SDL3-GL softpipe context+frame PASS (software renderer: validates Play's GL plumbing, not d3d12)"
else
    echo "PLAY-SDL3-GL no Mesa in the runtime directory (PLAY_MESA=0 build): Play's WGL stubs answered; window/context BLOCKED"
fi
