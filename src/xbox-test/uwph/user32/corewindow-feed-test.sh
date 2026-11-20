#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run after build.sh. Host Wine only; never contacts the console.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# A NixOS mingw gcc keeps libstdc++'s gthread behind mcfgthread; point at the
# dev output when the environment names it (a distro mingw needs neither).
extra=()
[[ -n ${MCFG_INC:-} ]] && extra+=(-I"$MCFG_INC")
[[ -n ${MCFG_LIB:-} ]] && extra+=(-L"$MCFG_LIB")
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static "${extra[@]}" corewindow_feed_test.cpp -o "$work/corewindow-feed-test.exe"
cp out/layout/uwph-u32c.dll "$work/"
WINEDEBUG=-all wine "$work/corewindow-feed-test.exe" "$(winepath -w "$work/uwph-u32c.dll")"
