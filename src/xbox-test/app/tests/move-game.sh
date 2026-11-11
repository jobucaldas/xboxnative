#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device evidence for the storage move job (move_game.h) on a real
# filesystem under Wine: plan refusals, copy/verify, marker rewrite order,
# cancel and failure cleanup, overwrite refusal. Mirrors game-delete.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
out=out/move-game-tests
rm -rf "$out"
mkdir -p "$out/root"
# -static like game-delete.sh/storage-report.sh: CI installs no mingw runtime
# DLLs, and a non-static exe dies there with STATUS_DLL_NOT_FOUND (exit 53)
# before printing any test output.
"${CONFIG_CXX:-x86_64-w64-mingw32-g++}" -std=c++17 -O2 -Wall -Wextra -static ${CONFIG_CXX_FLAGS:-} tests/move-game.cpp -o "$out/move-game.exe"
root=$(winepath -w "$out/root" | tr -d '\r')
# The mingw runtime DLLs must sit beside the exe for Wine to load it
# (deps built from a pinned source; never committed — rule 5/9).
if [ -n "${MINGW_RUNTIME_DIR:-}" ]; then
  cp "$MINGW_RUNTIME_DIR"/libgcc_s_seh-1.dll "$MINGW_RUNTIME_DIR"/libstdc++-6.dll "$MINGW_RUNTIME_DIR"/libmcfgthread-2.dll "$out/" 2>/dev/null || true
fi
WINEDEBUG=-all timeout -k 2 120 wine "$out/move-game.exe" "$root" | tr -d '\r' | tee "$out/result.log"
grep -q '^MOVE-GAME OK' "$out/result.log"
