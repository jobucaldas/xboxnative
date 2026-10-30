#!/usr/bin/env bash
# Synthetic PE import-table fixtures and off-device mapper resolver harness.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
trap 'rc=$?; echo "FAIL stage line=$LINENO status=$rc command=$BASH_COMMAND" >&2' ERR
cd "$(dirname "$0")/.."
mode=${1:---build-only}
if [[ "$mode" != --build-only && "$mode" != --run ]]; then
  echo "usage: $0 [--build-only|--run]" >&2
  exit 2
fi
tc=/data/OpenBot/Shared/toolchains/llvm-mingw-20260908-ucrt-ubuntu-22.04-x86_64/bin
export PATH="/data/OpenBot/Shared/toolchains/winehq-noble/bin:$tc:$PATH"
export WINEPREFIX=/data/OpenBot/Agents/agent-fb2e31d9-cf59-41da-b641-a7693a10395e/wine-prefix-strict-imports
export WINEARCH=win64
clang="$tc/x86_64-w64-mingw32-clang"
objdump="$tc/x86_64-w64-mingw32-objdump"
dlltool="$tc/llvm-dlltool"
out=out-strict-imports
mkdir -p "$out"
src=tests/strict-imports

"$clang" -shared -O0 -Wl,--out-implib,"$out/strict-provider.lib" -o "$out/strict-provider.dll" "$src/provider.c" "$src/provider.def"
"$dlltool" -d "$src/missing-name.def" -l "$out/missing-name.lib" -D strict-provider.dll
"$dlltool" -d "$src/missing-ordinal.def" -l "$out/missing-ordinal.lib" -D strict-provider.dll
"$dlltool" -d "$src/host-callback.def" -l "$out/host-callback.lib" -D strict-host-callback.dll
"$clang" -c -O0 "$src/importer.c" -o "$out/importer.obj"
"$clang" -c -O0 "$src/missing-name.c" -o "$out/missing-name.obj"
"$clang" -c -O0 "$src/missing-ordinal.c" -o "$out/missing-ordinal.obj"
"$tc/lld-link" /dll /nodefaultlib /entry:fixture_dll_entry /machine:x64 /dynamicbase /nxcompat /out:"$out/importer.dll" "$out/importer.obj" "$out/strict-provider.lib" "$out/host-callback.lib"
"$tc/lld-link" /dll /nodefaultlib /entry:missing_name_dll_entry /machine:x64 /dynamicbase /nxcompat /out:"$out/missing-name.dll" "$out/missing-name.obj" "$out/missing-name.lib" "$out/host-callback.lib"
"$tc/lld-link" /dll /nodefaultlib /entry:missing_ordinal_dll_entry /machine:x64 /dynamicbase /nxcompat /out:"$out/missing-ordinal.dll" "$out/missing-ordinal.obj" "$out/missing-ordinal.lib" "$out/host-callback.lib"

"$objdump" -p "$out/strict-provider.dll" > "$out/provider-tables.txt"
"$objdump" -p "$out/importer.dll" > "$out/importer-tables.txt"
"$objdump" -p "$out/missing-name.dll" > "$out/missing-name-tables.txt"
"$objdump" -p "$out/missing-ordinal.dll" > "$out/missing-ordinal-tables.txt"
grep -q 'fixture_named' "$out/provider-tables.txt"
grep -q '^[[:space:]]*7[[:space:]]\+0x' "$out/provider-tables.txt" || { echo "FAIL provider ordinal-only-7 export assertion: $out/provider-tables.txt" >&2; exit 1; }
grep -q 'fixture_named' "$out/importer-tables.txt"
grep -q '^[[:space:]]*7$' "$out/importer-tables.txt" || { echo "FAIL import-table ordinal-only-7 assertion: $out/importer-tables.txt" >&2; exit 1; }
grep -q 'absent_name' "$out/missing-name-tables.txt"
grep -q 'strict-provider.dll' "$out/missing-name-tables.txt"
grep -q '^[[:space:]]*9$' "$out/missing-ordinal-tables.txt"
grep -q 'strict-provider.dll' "$out/missing-ordinal-tables.txt"
if grep -q 'absent_name\|absent_ordinal' "$out/provider-tables.txt"; then
  echo "FAIL: negative imports unexpectedly appear in provider exports" >&2
  exit 1
fi
echo "BUILD-ONLY PASS: provider and positive/negative PE tables inspected; negative imports absent from provider exports; no resolver execution or guest entry tested"
if [[ "$mode" == --run ]]; then
  "$clang++" -std=c++17 -O2 -static tests/strict-imports-host.cpp -o "$out/strict-imports-host.exe"
  env -u DP_PASS -u TS_AUTHKEY WINEDEBUG=-all wine "$out/strict-imports-host.exe" \
    "$out/importer.dll" "$out/missing-name.dll" "$out/missing-ordinal.dll"
fi
