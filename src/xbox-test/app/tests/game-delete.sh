#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device evidence for Delete game (installs.h) on a real filesystem under Wine.
set -euo pipefail
cd "$(dirname "$0")/.."
out=out/game-delete-tests
rm -rf "$out"; mkdir -p "$out"
"${CONFIG_CXX:-x86_64-w64-mingw32-g++}" -std=c++17 -O2 -Wall -Wextra -static tests/game-delete.cpp -o "$out/game-delete.exe"
root=$(WINEDEBUG=-all winepath -w "$out/root")
WINEDEBUG=-all timeout -k 2 30 wine "$out/game-delete.exe" "$root" | tr -d '\r' | tee "$out/result.log"
grep -q '^GAME-DELETE PASS: ' "$out/result.log"
