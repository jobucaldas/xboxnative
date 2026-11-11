#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the legacy->flip swap-chain conversion test and runs it under Wine.
# The conversion is host logic (no D3D device), so Wine answers enough of
# DXGI's flat API for the test to run headless.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out/graphics-tests
# NixOS keeps the mingw thread library out of the compiler's default search
# path; CI's Debian mingw has it there. Add every store copy that exists.
extra=()
for d in /nix/store/*-mcfgthread-x86_64-w64-mingw32-*/lib; do [[ -d $d ]] && extra+=("-L$d"); done
x86_64-w64-mingw32-g++ -std=c++17 -O2 tests/swap-chain-convert.cpp -o out/graphics-tests/swap-chain-convert.exe -ldxgi -ldxguid "${extra[@]}"
# Wine refuses a prefix under /tmp on CI (not owned by the runner); default
# to a directory beside the build output like the registry selftest does.
export WINEPREFIX=${WINEPREFIX:-$PWD/out/graphics-tests/wine-prefix}
WINEDEBUG=-all timeout -k 2 30 wine out/graphics-tests/swap-chain-convert.exe
