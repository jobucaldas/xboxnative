#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Cross-compiles x86-play-locale.cpp with the x86_64 mingw toolchain and runs it
# under wine (the host-side play_locale_call contract is Windows API behavior).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cxx="${X64_CXX:-$(command -v x86_64-w64-mingw32-g++ || true)}"
if [[ -z $cxx ]]; then cxx=$(ls -d /nix/store/*-x86_64-w64-mingw32-gcc-wrapper-*/bin/x86_64-w64-mingw32-g++ 2>/dev/null | head -n1 || true); fi
[[ -n $cxx ]] || { echo "x86-play-locale: no x86_64-w64-mingw32-g++ (set X64_CXX)" >&2; exit 1; }
wine="${WINE:-$(command -v wine || true)}"
[[ -n $wine ]] || { echo "x86-play-locale: no wine (set WINE)" >&2; exit 1; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-play-locale.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
# NixOS: the mcfgthread headers and libs of the mingw gcc live in the store.
extra=()
for d in /nix/store/*-mcfgthread-x86_64-w64-mingw32-*-dev/include; do [[ -d $d ]] && extra+=("-I$d"); done
for d in /nix/store/*-mcfgthread-x86_64-w64-mingw32-*/lib; do [[ -d $d ]] && extra+=("-L$d"); done
"$cxx" -std=c++17 -O2 -static "${extra[@]}" "$here/x86-play-locale.cpp" -o "$tmp/x86-play-locale.exe"
export WINEPREFIX="${WINEPREFIX:-${XBN_WINEPREFIX:-$HOME/.cache/xbn-wine/x86-play-locale}}" WINEDEBUG=-all
mkdir -p "$WINEPREFIX"
"$wine" "$tmp/x86-play-locale.exe"
