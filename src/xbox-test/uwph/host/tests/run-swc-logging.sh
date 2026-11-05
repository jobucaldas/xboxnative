#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# OFF-DEVICE caller/logging regression, not a rendering or Xbox result.
set -euo pipefail
cd "$(dirname "$0")/.."
export WINEPREFIX="${WINEPREFIX:-$PWD/out-tls/tests/wine-prefix}"
mkdir -p out-tls/tests "$WINEPREFIX"
cc=${CC:-x86_64-w64-mingw32-gcc}
cxx=${CXX:-x86_64-w64-mingw32-g++}
link=${LLD_LINK:-lld-link}
if ! command -v "$link" >/dev/null && command -v lld-link-19 >/dev/null; then link=lld-link-19; fi
kernel32=$($cc -print-file-name=libkernel32.a)
[[ -f $kernel32 ]]
"$cc" -O1 -fno-stack-protector -c tests/swc-log-guest.c -o out-tls/tests/swc-log-guest.obj
"$link" /nodefaultlib /entry:swc_log_entry /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:out-tls/tests/swc-log-guest.exe out-tls/tests/swc-log-guest.obj "$kernel32"
"$cxx" -std=c++17 -O2 -static tests/swc-logging.cpp -o out-tls/tests/swc-logging.exe -ld3d11 -ldxgi -ldxguid
for mode in fresh nested; do
    env -u DP_PASS -u TS_AUTHKEY WINEDEBUG=-all timeout -k 2 30 wine out-tls/tests/swc-logging.exe out-tls/tests/swc-log-guest.exe "$mode"
done
