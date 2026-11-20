#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device multimedia-timer regression (Wine); never contacts the console.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
# A NixOS mingw gcc keeps libstdc++'s gthread behind mcfgthread; point at the
# dev output when the environment names it (a distro mingw needs neither).
extra=()
[[ -n ${MCFG_INC:-} ]] && extra+=(-I"$MCFG_INC")
[[ -n ${MCFG_LIB:-} ]] && extra+=(-L"$MCFG_LIB")
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static "${extra[@]}" timer-test.cpp -o "$work/timer-test.exe"
WINEDEBUG=-all wine "$work/timer-test.exe"
