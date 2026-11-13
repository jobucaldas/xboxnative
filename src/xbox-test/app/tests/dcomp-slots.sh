#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# DComp vtable-slot audit (bugbot r4559524004): main.cpp's dead page drives
# DirectComposition through the REAL <dcomp.h>, but the slots it relies on
# must match the header CI compiles against — a hand-counted slot forwarded
# SetContent into the middle of the real vtable (hand-count said 21, the
# header says 15; the recurring hand-declared-Windows-type bug class).
#
# This test includes the real mingw-w64 <dcomp.h> and derives each slot from
# the header itself with a pointer-to-member-function walker (the Itanium ABI
# stores the vtable byte offset in the PMF's ptr field; /8 gives the slot
# number). A drifted header or a mis-remembered slot fails here instead of
# forwarding to the wrong method on the console. Compiles with mingw g++
# (the same family main.cpp's UWP crossbuild uses), runs under Wine headless.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out/graphics-tests
# Statically linked like guest-watchdog.sh: Wine never searches a Linux gcc
# lib dir, so a dynamic libstdc++/libwinpthread would be a start-up risk.
extra=()
for d in /nix/store/*-mcfgthread-x86_64-w64-mingw32-*/lib; do [[ -d $d ]] && extra+=("-L$d"); done
x86_64-w64-mingw32-g++ -std=c++20 -O2 -static tests/dcomp-slots.cpp -o out/graphics-tests/dcomp-slots.exe "${extra[@]}"
WINEDEBUG=-all timeout -k 2 30 wine out/graphics-tests/dcomp-slots.exe
