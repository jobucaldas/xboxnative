#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# fna-driver.sh          POSIX build (c++ or $CXX), run natively.
# fna-driver.sh --wine   Win32 build (x86_64-w64-mingw32-g++), run under Wine:
#                        exercises the Get/SetEnvironmentVariableW path.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
tmp=$(mktemp -d /tmp/fna-driver.XXXXXX)
trap 'rm -rf "$tmp"' EXIT
if [[ ${1:-} == --wine ]]; then
  x86_64-w64-mingw32-g++ -std=c++17 -Wall -Wextra -Werror -static "$here/fna-driver.cpp" -o "$tmp/fna-driver.exe"
  WINEDEBUG=-all wine "$tmp/fna-driver.exe" | tr -d '\r'
  exit "${PIPESTATUS[0]}"
fi
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread "$here/fna-driver.cpp" -o "$tmp/fna-driver"
"$tmp/fna-driver"
