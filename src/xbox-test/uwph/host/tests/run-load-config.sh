#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device mapper regression; not UWP/console evidence.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
o="$here/out-load-config"; mkdir -p "$o"
cc=${CONFIG_CC:-x86_64-w64-mingw32-gcc}
cxx=${CONFIG_CXX:-x86_64-w64-mingw32-g++}
link=${CONFIG_LINKER:-lld-link}
"$cc" -c -O1 -fno-stack-protector "$here/tests/load-config-guest.c" -o "$o/guest.obj"
lib=$($cc -print-file-name=libkernel32.a)
"$link" /dll /nodefaultlib /entry:config_entry /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$o/guest.dll" "$o/guest.obj" "$lib"
"$cxx" -std=c++17 -O2 -static "$here/tests/load-config-test.cpp" -o "$o/mapper-test.exe"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$o/wine-prefix}
env -u DP_PASS -u TS_AUTHKEY wine "$o/mapper-test.exe" "$o/guest.dll" "$o" | tr -d '\r' | tee "$o/result.log"
grep -Fx 'WINE-CONFIG-MAPPER PASS: explicit inert opt-in, default rejection, 9 pre-allocation negatives' "$o/result.log"
grep -Fx 'WINE-CONFIG-MAPPER PASS: 5 pre-DllMain write-into-record negatives, adjacent relocation, retry' "$o/result.log"
