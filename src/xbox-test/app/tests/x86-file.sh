#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cc32="${I686_CXX:-$(command -v i686-w64-mingw32-g++ || true)}"
if [[ -z $cc32 ]]; then cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-g++ 2>/dev/null | head -n1 || true); fi
[[ -n $cc32 ]] || { echo "x86-file: no i686-w64-mingw32-g++ (set I686_CXX)" >&2; exit 1; }
"$cc32" -std=c++17 -Wall -Wextra -Werror -DFILE_DECLARATIONS_ONLY -fsyntax-only "$here/x86-file.cpp"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-file.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror "$here/x86-file.cpp" -o "$tmp/x86-file"
"$tmp/x86-file"
