#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device multimedia-timer regression (Wine); never contacts the console.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static timer-test.cpp -o "$work/timer-test.exe"
WINEDEBUG=-all wine "$work/timer-test.exe"
