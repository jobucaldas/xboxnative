#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds two PE32 fixtures (one with a VERSIONINFO resource, one without) and runs the version route checks.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
if [[ -z $cc32 ]]; then cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true); fi
[[ -n $cc32 ]] || { echo "x86-version: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
windres="$(dirname "$cc32")/i686-w64-mingw32-windres"
[[ -x $windres ]] || { echo "x86-version: no i686-w64-mingw32-windres next to $cc32" >&2; exit 1; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-version.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
cat >"$tmp/version.rc" <<'EOF'
1 VERSIONINFO
FILEVERSION 1,2,3,4
PRODUCTVERSION 1,2,3,4
FILEFLAGSMASK 0x3fL
FILEFLAGS 0x0L
FILEOS 0x40004L
FILETYPE 0x1L
FILESUBTYPE 0x0L
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904b0"
    BEGIN
      VALUE "ProductName", "XbnFixture"
      VALUE "FileVersion", "1.2.3.4"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x0409, 1200
  END
END
EOF
printf 'void entry(void) { }\n' >"$tmp/main.c"
"$windres" -O coff "$tmp/version.rc" -o "$tmp/version.o"
"$cc32" -ffreestanding -nostdlib -Wl,--entry,_entry -Wl,--subsystem,console -o "$tmp/fixture.exe" "$tmp/main.c" "$tmp/version.o" -lkernel32
"$cc32" -ffreestanding -nostdlib -Wl,--entry,_entry -Wl,--subsystem,console -o "$tmp/plain.exe" "$tmp/main.c" -lkernel32
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$here/x86-version.cpp" -o "$tmp/x86-version"
"$tmp/x86-version" "$tmp/fixture.exe" "$tmp/plain.exe"
