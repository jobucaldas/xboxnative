#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device SHGetSpecialFolderPathW parity regression (Wine); never contacts the console.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static special-folder-test.cpp -o "$work/special-folder-test.exe" -lshell32 -lole32 -luuid
WINEDEBUG=-all "${WINE_BIN:-wine}" "$work/special-folder-test.exe"
