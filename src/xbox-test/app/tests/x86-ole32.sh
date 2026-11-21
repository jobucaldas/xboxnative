#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds and runs the focused guest OLE32 route contract test.
# x86-ole32.sh          POSIX build (c++ or $CXX), run natively.
# x86-ole32.sh --wine   Win32 build (x86_64-w64-mingw32-g++), run under Wine:
#                       also exercises deferred DirectSound COM initialization.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-ole32.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
if [[ ${1:-} == --wine ]]; then
  cc64="${X86_64_CXX:-$(command -v x86_64-w64-mingw32-g++ || true)}"
  [[ -n $cc64 ]] || { echo "x86-ole32 FAIL: need x86_64-w64-mingw32-g++" >&2; exit 1; }
  extra64=()
  for d in /nix/store/*-mcfgthread-x86_64-w64-mingw32-*; do
    [[ -d $d/include ]] && extra64+=("-I$d/include")
    [[ -d $d/lib ]] && extra64+=("-L$d/lib")
  done
  "$cc64" -std=c++17 -O1 -mno-red-zone -Wall -Wextra -Werror \
    -Wno-error=cast-function-type -Wno-error=misleading-indentation -Wno-error=unused-parameter -Wno-error=sign-compare \
    "${extra64[@]}" -static -static-libgcc -static-libstdc++ \
    "$here/x86-ole32.cpp" -o "$tmp/x86-ole32.exe" -lws2_32 -luuid
  wine="${WINE:-wine}"
  command -v "$wine" >/dev/null || { echo "x86-ole32 FAIL: Wine not found" >&2; exit 1; }
  export WINEPREFIX="${WINEPREFIX:-/tmp/xbn-x86-ole32-wine}"
  mkdir -p "$WINEPREFIX"
  WINEDEBUG=-all "$wine" "$tmp/x86-ole32.exe" | tr -d '\r'
  exit "${PIPESTATUS[0]}"
fi
"${CXX:-c++}" -std=c++17 -O1 -mno-red-zone -Wall -Wextra -Werror "$here/x86-ole32.cpp" -o "$tmp/x86-ole32"
"$tmp/x86-ole32"
