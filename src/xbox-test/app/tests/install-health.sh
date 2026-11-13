#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device evidence for install health (installs.h payload_state): a
# dl_<appid>.done marker beside a missing or empty folder is not an installed
# game. Runs on a real filesystem under Wine; mirrors game-delete.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
out=out/install-health-tests
rm -rf "$out"; mkdir -p "$out"
# -static like game-delete.sh/move-game.sh: CI installs no mingw runtime DLLs.
"${CONFIG_CXX:-x86_64-w64-mingw32-g++}" -std=c++17 -O2 -Wall -Wextra -static ${CONFIG_CXX_FLAGS:-} tests/install-health.cpp -o "$out/install-health.exe"
root=$(WINEDEBUG=-all winepath -w "$out/root")
WINEDEBUG=-all timeout -k 2 30 wine "$out/install-health.exe" "$root" | tr -d '\r' | tee "$out/result.log"
grep -q '^INSTALL-HEALTH OK' "$out/result.log"
