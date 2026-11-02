#!/usr/bin/env bash
# Synthetic PE import-table fixtures and off-device mapper resolver harness.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
trap 'rc=$?; echo "FAIL stage line=$LINENO status=$rc command=$BASH_COMMAND" >&2' ERR
cd "$(dirname "$0")/.."
mode=${1:---build-only}
if (( $# > 1 )) || [[ "$mode" != --build-only && "$mode" != --run ]]; then
  echo "usage: $0 [--build-only|--run]" >&2
  exit 2
fi
if [[ -n ${LLVM_MINGW_ROOT:-} ]]; then export PATH="$LLVM_MINGW_ROOT/bin:$PATH"; fi
clang=${STRICT_CC:-x86_64-w64-mingw32-clang}
clangxx=${STRICT_CXX:-x86_64-w64-mingw32-clang++}
if [[ -z ${STRICT_CC:-} ]] && ! command -v "$clang" >/dev/null 2>&1; then clang=x86_64-w64-mingw32-gcc; fi
if [[ -z ${STRICT_CXX:-} ]] && ! command -v "$clangxx" >/dev/null 2>&1; then clangxx=x86_64-w64-mingw32-g++; fi
# The PE-table assertions use LLVM's output format, including ordinal-only rows.
objdump=${STRICT_OBJDUMP:-llvm-objdump}
dlltool=${STRICT_DLLTOOL:-llvm-dlltool}
linker=${STRICT_LINKER:-lld-link}
required=("$clang" "$objdump" "$dlltool" "$linker")
if [[ "$mode" == --run ]]; then
  required+=("$clangxx" node)
fi
for tool in "${required[@]}"; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "FAIL missing tool: $tool (add MinGW/LLVM to PATH or set LLVM_MINGW_ROOT)" >&2
    exit 1
  fi
done
if [[ "$mode" == --run ]]; then
  wine=${WINE_BIN:-wine}
  if ! command -v "$wine" >/dev/null 2>&1; then
    echo "FAIL missing Wine: $wine (set WINE_BIN or add wine to PATH)" >&2
    exit 1
  fi
fi
out='out-strict-imports'
mkdir -p "$out"
src=tests/strict-imports

"$clang" -shared -O0 -Wl,--out-implib,"$out/strict-provider.lib" -o "$out/strict-provider.dll" "$src/provider.c" "$src/provider.def"
"$dlltool" -d "$src/missing-name.def" -l "$out/missing-name.lib" -D strict-provider.dll
"$dlltool" -d "$src/missing-ordinal.def" -l "$out/missing-ordinal.lib" -D strict-provider.dll
"$dlltool" -d "$src/host-callback.def" -l "$out/host-callback.lib" -D strict-host-callback.dll
"$clang" -c -O0 "$src/importer.c" -o "$out/importer.obj"
"$clang" -c -O0 "$src/missing-name.c" -o "$out/missing-name.obj"
"$clang" -c -O0 "$src/missing-ordinal.c" -o "$out/missing-ordinal.obj"
"$linker" /dll /nodefaultlib /entry:fixture_dll_entry /machine:x64 /dynamicbase /nxcompat /out:"$out/importer.dll" "$out/importer.obj" "$out/strict-provider.lib" "$out/host-callback.lib"
"$linker" /dll /nodefaultlib /entry:missing_name_dll_entry /machine:x64 /dynamicbase /nxcompat /out:"$out/missing-name.dll" "$out/missing-name.obj" "$out/missing-name.lib" "$out/host-callback.lib"
"$linker" /dll /nodefaultlib /entry:missing_ordinal_dll_entry /machine:x64 /dynamicbase /nxcompat /out:"$out/missing-ordinal.dll" "$out/missing-ordinal.obj" "$out/missing-ordinal.lib" "$out/host-callback.lib"

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
if grep -q 'absent_name\|absent_ordinal' "$out/provider-tables.txt" ||
   grep -Eq '^[[:space:]]*9[[:space:]]+0x' "$out/provider-tables.txt"; then
  echo "FAIL: negative imports unexpectedly appear in provider exports" >&2
  exit 1
fi
for fixture in importer missing-name missing-ordinal; do
  grep -Eq '^Entry 5 [0-9a-f]*[1-9a-f][0-9a-f]* [0-9a-f]*[1-9a-f][0-9a-f]* Base Relocation Directory' "$out/$fixture-tables.txt" || { echo "FAIL $fixture nonempty relocation directory assertion" >&2; exit 1; }
done
echo "BUILD-ONLY PASS: provider and positive/negative PE tables inspected; relocation directories nonempty; negative imports absent from provider exports; no resolver execution or guest entry tested"
if [[ "$mode" == --run ]]; then
  "$clang" -O1 -c tests/lifecycle-tls.c -o "$out/delay-source.obj"
  kernel32=$("$clang" -print-file-name=libkernel32.a)
  [[ -f "$kernel32" ]] || { echo "FAIL missing libkernel32.a: $kernel32" >&2; exit 1; }
  "$linker" /dll /nodefaultlib /entry:test_dll_entry /include:_tls_used /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/delay-source.dll" "$out/delay-source.obj" "$kernel32"
  node tests/make-delay-fixture.js "$out/delay-source.dll" "$out/delay-tls.dll"
  node tests/make-delay-fixture.js "$out/delay-source.dll" "$out/ordinary-zero.dll" ordinary-zero
  node tests/make-delay-fixture.js "$out/delay-source.dll" "$out/delay-zero.dll" delay-zero
  "$clangxx" -std=c++17 -O2 -static tests/strict-imports-host.cpp -o "$out/strict-imports-host.exe"
  export WINEPREFIX="${WINEPREFIX:-$PWD/$out/wine-prefix}"
  export WINEARCH=win64
  env -u DP_PASS -u TS_AUTHKEY WINEDEBUG=-all "$wine" "$out/strict-imports-host.exe" \
    "$out/importer.dll" "$out/missing-name.dll" "$out/missing-ordinal.dll" "$out/delay-tls.dll" "$out/ordinary-zero.dll" "$out/delay-zero.dll"
fi
