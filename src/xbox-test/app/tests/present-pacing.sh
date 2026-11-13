#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds and runs the present-pacing contract test (host/present_pacing.h):
# the present.txt parser (off/missing = disabled, malformed keys fall back to
# off, sync clamped to 0..4), the SyncInterval override, and the fps-cap
# limiter's slot spacing on a fake clock. The header is pure (no
# WinRT/COM/windows.h), so this needs no Wine and no device.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out/graphics-tests
# Statically linked like drain-order.sh: NixOS keeps the mingw thread library
# out of the compiler's default search path; CI's Debian mingw ships it in the
# static runtime already.
extra=()
for d in /nix/store/*-mcfgthread-x86_64-w64-mingw32-*/lib; do [[ -d $d ]] && extra+=("-L$d"); done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static tests/present-pacing.cpp -o out/graphics-tests/present-pacing.exe "${extra[@]}"
WINEDEBUG=-all timeout -k 2 30 wine out/graphics-tests/present-pacing.exe
