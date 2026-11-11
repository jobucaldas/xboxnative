#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds and runs the resize-drain order contract test (host/resize_drain.h):
# the drain must End the query, Flush to submit it, and only then poll
# GetData — the Bugbot HIGH finding this pins (wait before submit = blind
# sleep). Runs under Wine headless; no D3D device is created.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out/graphics-tests
# Statically linked like graphics-factory.sh: Wine never searches a Linux
# gcc lib dir, so a dynamic libstdc++/libwinpthread would be a start-up risk.
# NixOS keeps the mingw thread library out of the compiler's default search
# path; CI's Debian mingw ships it in the static runtime already.
extra=()
for d in /nix/store/*-mcfgthread-x86_64-w64-mingw32-*/lib; do [[ -d $d ]] && extra+=("-L$d"); done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static tests/drain-order.cpp -o out/graphics-tests/drain-order.exe -ld3d11 "${extra[@]}"
WINEDEBUG=-all timeout -k 2 30 wine out/graphics-tests/drain-order.exe
