#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds and runs the focused guest user32 route contract test.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
tmp=$(mktemp -d /tmp/x86-user32.XXXXXX); trap 'rm -rf "$tmp"' EXIT
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
[[ -n $cc32 ]] || { echo "x86-user32: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
"$cc32" -O1 -Wall -Wextra -Werror -Wno-cast-function-type -nostdlib -Wl,--entry,entry@0 -Wl,--subsystem,console \
  -o "$tmp/x86-user32-guest.exe" "$here/x86-user32-guest.c" -lkernel32 -luser32
"${CXX:-c++}" -std=c++17 -O1 -mno-red-zone -Wall -Wextra -Werror "$here/x86-user32.cpp" -o "$tmp/x86-user32"
"$tmp/x86-user32" "$tmp/x86-user32-guest.exe"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror "$here/x86-thread-timer.cpp" -o "$tmp/x86-thread-timer"
"$tmp/x86-thread-timer"
