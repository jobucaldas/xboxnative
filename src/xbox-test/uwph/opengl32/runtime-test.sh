#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run after app/build.sh (or play-runtime.sh): needs app/out/layout and app/out/obj. A guest
# loading OPENGL32.DLL at run time must get Play's WGL stubs, never a host DLL.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
CB=${UWP_CROSSBUILD:-out/uwp-crossbuild}
x86_64-w64-mingw32-g++ -c -O2 -fno-stack-protector runtime-guest.cpp -o "$work/guest.obj"
lld-link-19 /nodefaultlib /entry:gl_entry /include:gl_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$work/play-guest.exe" "$work/guest.obj" /usr/x86_64-w64-mingw32/lib/libkernel32.a
app=../../app; bash ../host/build-sqlite-lib.sh "$work/sqlite3.lib" >/dev/null
UWP_OBJ_DIR="$work/obj" "$CB/scripts/build.sh" --uwp --link-arg "$work/sqlite3.lib" --out "$work/runner.exe" runtime-runner.cpp -- /MT /O2 "/I$app/out/obj" /I../host/third_party/sqlite
cp ../../app/out/layout/play-tls-*.dll ../../app/out/layout/uwph-u32c.dll "$work/"
WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 20 wine "$work/runner.exe"
