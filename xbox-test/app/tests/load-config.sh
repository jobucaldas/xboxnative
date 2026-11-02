#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device evidence only; no Xbox or gameplay claim.
set -euo pipefail
cd "$(dirname "$0")/.."
out=out/load-config-tests
mkdir -p "$out"
cc=${CONFIG_CC:-x86_64-w64-mingw32-gcc}
cxx=${CONFIG_CXX:-x86_64-w64-mingw32-g++}
link=${CONFIG_LINKER:-lld-link}
"$cc" -c -O1 -fno-stack-protector ../uwph/host/tests/load-config-guest.c -o "$out/guest.obj"
"$link" /dll /nodefaultlib /entry:config_entry /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/guest.dll" "$out/guest.obj" "$("$cc" -print-file-name=libkernel32.a)"
"$cxx" -std=c++17 -O2 -static tests/load-config.cpp -o "$out/mapper-test.exe"
WINEDEBUG=-all timeout -k 2 30 wine "$out/mapper-test.exe" "$out/guest.dll" "$out" | tr -d '\r' | tee "$out/result.log"
grep -Fx 'WINE-CONFIG-MAPPER PASS: explicit inert opt-in, default rejection, 9 pre-allocation negatives' "$out/result.log"
