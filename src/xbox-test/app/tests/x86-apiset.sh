#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# API-set imports for the x86 binder (host/x86/apiset.h), off-device. The
# fixture imports strlen through api-ms-win-crt-string-l1-1-0.dll (must bind
# and return 5) and frobnicate through api-ms-win-foo-l1-1-0.dll (must stay
# unresolved and be named by the gap list). The two import libraries are built
# here with dlltool from .def files, so no binary is committed.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
if [[ -z $cc32 ]]; then
  cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true)
fi
[[ -n $cc32 ]] || { echo "x86-apiset: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
dlltool="$(dirname "$cc32")/i686-w64-mingw32-dlltool"
[[ -x $dlltool ]] || { echo "x86-apiset: no i686-w64-mingw32-dlltool next to the compiler" >&2; exit 1; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-apiset.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
printf 'LIBRARY api-ms-win-crt-string-l1-1-0.dll\nEXPORTS\nstrlen\n' > "$tmp/crt.def"
printf 'LIBRARY api-ms-win-foo-l1-1-0.dll\nEXPORTS\nfrobnicate\n' > "$tmp/foo.def"
printf 'LIBRARY api-ms-win-crt-time-l1-1-0.dll\nEXPORTS\ntime\n' > "$tmp/time.def"
printf 'LIBRARY api-ms-win-crt-locale-l1-1-0.dll\nEXPORTS\nlocaleconv\n' > "$tmp/locale.def"
printf 'LIBRARY api-ms-win-crt-filesystem-l1-1-0.dll\nEXPORTS\n_get_osfhandle\n_lseeki64\n' > "$tmp/fs.def"
"$dlltool" -d "$tmp/crt.def" -l "$tmp/libapi-ms-win-crt-string-l1-1-0.a" -D api-ms-win-crt-string-l1-1-0.dll
"$dlltool" -d "$tmp/foo.def" -l "$tmp/libapi-ms-win-foo-l1-1-0.a" -D api-ms-win-foo-l1-1-0.dll
"$dlltool" -d "$tmp/time.def" -l "$tmp/libapi-ms-win-crt-time-l1-1-0.a" -D api-ms-win-crt-time-l1-1-0.dll
"$dlltool" -d "$tmp/locale.def" -l "$tmp/libapi-ms-win-crt-locale-l1-1-0.a" -D api-ms-win-crt-locale-l1-1-0.dll
"$dlltool" -d "$tmp/fs.def" -l "$tmp/libapi-ms-win-crt-filesystem-l1-1-0.a" -D api-ms-win-crt-filesystem-l1-1-0.dll
"$cc32" -O1 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables \
  -mno-sse -mno-mmx -mno-80387 -nostdlib -Wl,--entry,_apiset_start@0 -Wl,--subsystem,console -Wl,--dynamicbase \
  -Wl,--no-seh -Wl,--image-base,0x400000 -o "$tmp/apiset.exe" "$here/x86-fixture/apiset.c" \
  -L"$tmp" -lapi-ms-win-crt-string-l1-1-0 -lapi-ms-win-crt-time-l1-1-0 -lapi-ms-win-crt-filesystem-l1-1-0 \
  -lapi-ms-win-crt-locale-l1-1-0 \
  -lapi-ms-win-foo-l1-1-0 -lkernel32
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror "$here/x86-apiset.cpp" -o "$tmp/x86-apiset"
"$tmp/x86-apiset" "$tmp/apiset.exe"
