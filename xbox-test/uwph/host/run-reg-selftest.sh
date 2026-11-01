#!/usr/bin/env bash
# Off-device: reg_test.c against the regb registry builtins under Wine (no console).
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
cd "$(dirname "$0")"
o=${1:-out/reg-selftest}; mkdir -p "$o"
x86_64-w64-mingw32-gcc -c -O1 -DREG_DIRECT -fno-builtin reg_test.c -o "$o/reg_test.o"
bash fetch-sqlite.sh
SQF="-DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_SHARED_CACHE -DSQLITE_TEMP_STORE=3 -DSQLITE_DEFAULT_MEMSTATUS=0"
[ -f "$o/sqlite3.o" ] || x86_64-w64-mingw32-gcc -c -O1 $SQF third_party/sqlite/sqlite3.c -o "$o/sqlite3.o"
x86_64-w64-mingw32-g++ -std=c++17 -O1 -static -Ithird_party/sqlite -o "$o/selftest.exe" reg_builtin_selftest.cpp "$o/reg_test.o" "$o/sqlite3.o"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine}
rm -rf "$o/ls"; mkdir -p "$o/ls"
wine "$o/selftest.exe" "$(winepath -w "$o/ls")" | tr -d '\r' | tee "$o/selftest.log"   # Wine console output is CRLF
grep -Fx 'SELFTEST OK' "$o/selftest.log"
ls -l "$o/ls"
