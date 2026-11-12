#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds and runs the guest-watchdog decision test (host/guest_watchdog.h):
# the launcher must declare the guest dead on a contained fault or a finished
# guest thread immediately, and on present silence only past the threshold —
# the console wedge this pins (frozen frame, stale FPS counter, guide
# unreachable) is the launcher acting on none of these. Pure logic, no D3D;
# runs under Wine headless.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out/graphics-tests
# Statically linked like drain-order.sh: Wine never searches a Linux gcc lib
# dir, so a dynamic libstdc++/libwinpthread would be a start-up risk.
extra=()
for d in /nix/store/*-mcfgthread-x86_64-w64-mingw32-*/lib; do [[ -d $d ]] && extra+=("-L$d"); done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static tests/guest-watchdog.cpp -o out/graphics-tests/guest-watchdog.exe "${extra[@]}"
WINEDEBUG=-all timeout -k 2 30 wine out/graphics-tests/guest-watchdog.exe
