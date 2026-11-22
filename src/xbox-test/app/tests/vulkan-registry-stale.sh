#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device (Wine) regression for the Vulkan driver registry's replace-not-append rule (#177): installs that update
# must leave only the live manifests behind, never the earlier install's entries. Not a console claim.
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
tests=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
host=$tests/../../uwph/host
o=${1:-$tests/../out/vulkan-registry-stale}; mkdir -p "$o"; o=$(cd "$o" && pwd)
bash "$host/fetch-sqlite.sh"
SQF="-DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_SHARED_CACHE -DSQLITE_TEMP_STORE=3 -DSQLITE_DEFAULT_MEMSTATUS=0"
x86_64-w64-mingw32-gcc -c -O1 $SQF "$host/third_party/sqlite/sqlite3.c" -o "$o/sqlite3.o"
extra=()
[[ -n ${MCFG_INC:-} ]] && extra+=(-I"$MCFG_INC")
[[ -n ${MCFG_LIB:-} ]] && extra+=(-L"$MCFG_LIB")
"${CONFIG_CXX:-x86_64-w64-mingw32-g++}" -std=c++17 -O1 -static "${extra[@]}" -I"$host/third_party/sqlite" \
  -o "$o/vulkan-registry-stale.exe" "$tests/vulkan-registry-stale.cpp" "$o/sqlite3.o"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$o/wine-prefix}
d=$(mktemp -d "$o/run.XXXXXXXX")
timeout -k 2 120 wine "$o/vulkan-registry-stale.exe" "$(winepath -w "$d")" | tr -d '\r' | tee "$o/result.log"
grep -Fx 'VULKAN-REGISTRY OK' "$o/result.log"
