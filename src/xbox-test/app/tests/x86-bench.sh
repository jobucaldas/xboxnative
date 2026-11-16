#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds identical i686 and native benchmark kernels and runs the i686 DLL in x86.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
if [[ -z $cc32 ]]; then cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true); fi
[[ -n $cc32 ]] || { echo "x86-bench: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-bench.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
"$cc32" -O2 -Wall -Wextra -Werror -msse2 -shared -nostdlib -Wl,--entry,0 -Wl,--image-base,0x25000000 -o "$tmp/x86-bench.dll" "$here/x86-bench-kernels.c"
"${CC:-cc}" -O2 -Wall -Wextra -Werror -msse2 -c "$here/x86-bench-kernels.c" -o "$tmp/kernels.o"
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror "$here/x86-bench.cpp" "$tmp/kernels.o" -o "$tmp/x86-bench"
echo "X86-BENCH mode=interpreter"
"$tmp/x86-bench" "$tmp/x86-bench.dll"
echo "X86-BENCH mode=dbt"
X86_DBT=1 "$tmp/x86-bench" "$tmp/x86-bench.dll"
