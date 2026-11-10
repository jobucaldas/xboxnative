#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-diag-fixtures.sh OUTDIR -> OUTDIR/{fixture-crt.exe,play-vulkan-dzn.exe,
# play-d3d9.exe,play-d3d10.exe}, the console self-test's fixtures (selftest.h).
#
# These are the same guest programs tests/play-runtime.sh builds for the
# off-device runner, compiled from this repository's sources so the package
# carries no third-party binary. Each is a few KB. The 32-bit one is built with
# the i686 mingw CRT, as x86-guest.sh builds it; the 64-bit ones link
# /nodefaultlib and import only what Play routes.
#
# Toolchain: the mingw import libraries are found through the compiler itself
# (`-print-file-name`), never a hardcoded prefix; the versioned llvm tools fall
# back to their unversioned names, as the shim builds do.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:?output directory required}"
mkdir -p "$out"
work=$(mktemp -d "${TMPDIR:-/tmp}/diag-fixtures.XXXXXX")
trap 'rm -rf "$work"' EXIT

cc64="${MINGW64_CC:-$(command -v x86_64-w64-mingw32-g++ || true)}"
[[ -n $cc64 ]] || { echo "build-diag-fixtures: no x86_64-w64-mingw32-g++" >&2; exit 1; }
mingw_lib=$(dirname "$("$cc64" -print-file-name=libkernel32.a)")
lld() { lld-link-19 "$@" 2>/dev/null || lld-link "$@"; }
clangxx() { clang++-19 "$@" 2>/dev/null || clang++ "$@"; }

# The 64-bit fixtures: no CRT, no exceptions, no unwinding.
fixture_flags=(-O1 -fno-stack-protector -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables)
common=(/nodefaultlib /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat)
libs=("$mingw_lib/libkernel32.a" "$mingw_lib/libuser32.a")

clangxx --target=x86_64-w64-windows-gnu "${fixture_flags[@]}" -fno-builtin -c "$here/play-vulkan-dzn.cpp" -o "$work/vulkan-dzn.obj"
lld "${common[@]}" /entry:vk_dzn_entry /include:vk_relocation /out:"$out/play-vulkan-dzn.exe" \
  "$work/vulkan-dzn.obj" "${libs[@]}"

clangxx --target=x86_64-w64-windows-gnu "${fixture_flags[@]}" -c "$here/play-d3d9.cpp" -o "$work/d3d9.obj"
lld "${common[@]}" /entry:d3d9_entry /include:d3d9_relocation /out:"$out/play-d3d9.exe" \
  "$work/d3d9.obj" "$mingw_lib/libd3d9.a" "${libs[@]}"

clangxx --target=x86_64-w64-windows-gnu "${fixture_flags[@]}" -c "$here/play-d3d10.cpp" -o "$work/d3d10.obj"
lld "${common[@]}" /entry:d3d10_entry /include:d3d10_relocation /out:"$out/play-d3d10.exe" \
  "$work/d3d10.obj" "$mingw_lib/libd3d10.a" "$mingw_lib/libdxgi.a" "$mingw_lib/libuuid.a" "${libs[@]}"

# The 32-bit fixture: a mingw-CRT PE32 program the interpreter runs to exit 42
# (x86-guest.sh asserts the same binary's stdout and exit code under Wine).
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
if [[ -z $cc32 ]]; then
  # NixOS: the i686 mingw gcc wrapper lives in the store but is not on PATH.
  cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true)
fi
[[ -n $cc32 ]] || { echo "build-diag-fixtures: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
extra=()
for d in /nix/store/*-mcfgthread-i686-w64-mingw32-*/lib; do [[ -d $d ]] && extra+=("-L$d"); done
rc32="$(dirname "$cc32")/i686-w64-mingw32-windres"
[[ -x $rc32 ]] || { echo "build-diag-fixtures: no i686-w64-mingw32-windres" >&2; exit 1; }
# The .rc is preprocessed, and the wrapper's preprocessor is the cross gcc.
PATH="$(dirname "$cc32"):$PATH" "$rc32" -O coff -I "$here/x86-guest" -i "$here/x86-guest/fixture-crt.rc" -o "$work/fixture-crt.res"
"$cc32" -O1 -Wall -Wextra -Werror "${extra[@]}" -o "$out/fixture-crt.exe" "$here/x86-guest/fixture-crt.c" "$work/fixture-crt.res"

# A fixture that does not fit the package's budget is a build failure, not a
# surprise on the console.
for f in "$out"/*.exe; do
  size=$(stat -c%s "$f")
  [[ $size -lt 5242880 ]] || { echo "build-diag-fixtures: $(basename "$f") is $size bytes (limit 5 MB)" >&2; exit 1; }
done
ls -l "$out"
