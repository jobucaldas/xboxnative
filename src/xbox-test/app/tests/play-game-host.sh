#!/usr/bin/env bash
# Reproducible off-device diagnostic using Play's loader and owned game files.
# A recorded crash/missing import is diagnostic completion, not a playable game.
set -euo pipefail
[[ $# -ge 1 && $# -le 2 ]] || { echo 'usage: play-game-host.sh GAME_DIR [DIAGNOSTIC_USER32_DLL]'; exit 2; }
game_root=$(realpath "$1")
alternate=
if [[ $# == 2 ]];then alternate=$(realpath "$2");fi
cd "$(dirname "$0")/.."
[[ -f "$game_root/Megabonk.exe" && -f "$game_root/Megabonk_Data/il2cpp_data/Metadata/global-metadata.dat" ]] || { echo 'complete owned Megabonk installation required'; exit 2; }
export WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/prun-wine}
export WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d
bash tests/play-runtime.sh
out=out/play-tests
# Source-built donors from the launcher layout, as packaged.
mkdir -p "$out/donors"; cp out/layout/donors/*.dll "$out/donors/"
# An explicit alternate DLL is for cross-lane diagnosis only. Restore our layout
# afterward; this never changes the packaged launcher or its source files.
trap 'cp out/layout/uwph-u32c.dll "$out/"' EXIT
if [[ -n "$alternate" ]];then cp "$alternate" "$out/uwph-u32c.dll";fi
sha256sum "$out/uwph-u32c.dll" "$game_root/Megabonk.exe" "$game_root/UnityPlayer.dll" "$game_root/GameAssembly.dll" "$game_root/baselib.dll"
export PRUN_GAME_ROOT
PRUN_GAME_ROOT=$(winepath -w "$game_root")
rm -f "$out/host-game-events.log"
set +e
timeout -k 2 $(( ${PRUN_GAME_SECONDS:-10} + 15 )) wine "$out/play-runner.exe" game > "$out/host-game-stdout.log" 2>&1
result=$?
set -e
[[ -f "$out/host-game-events.log" ]] && tail -8 "$out/host-game-events.log"
echo "OFF-DEVICE diagnostic exit=$result; events=$out/host-game-events.log"
exit "$result"
