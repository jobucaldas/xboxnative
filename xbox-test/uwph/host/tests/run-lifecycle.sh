#!/usr/bin/env bash
# OFF-DEVICE Wine check, not Xbox evidence. No console traffic.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out-tls/tests
cc=${CC:-x86_64-w64-mingw32-gcc}
link=${LLD_LINK:-lld-link}
if ! command -v "$link" >/dev/null && command -v lld-link-19 >/dev/null; then link=lld-link-19; fi
kernel32=$($cc -print-file-name=libkernel32.a)
if [[ ! -f $kernel32 ]]; then echo "libkernel32.a not found: $kernel32" >&2; exit 1; fi
"$cc" -O1 -c tests/lifecycle-dll.c -o out-tls/tests/lifecycle.obj
"$link" /dll /nodefaultlib /entry:test_dll_entry /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:out-tls/tests/lifecycle.dll out-tls/tests/lifecycle.obj "$kernel32"
"$cc" -O1 -DFAIL_ATTACH -c tests/lifecycle-dll.c -o out-tls/tests/lifecycle-fail.obj
"$link" /dll /nodefaultlib /entry:test_dll_entry /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:out-tls/tests/lifecycle-fail.dll out-tls/tests/lifecycle-fail.obj "$kernel32"
for kind in tls tls-fail; do
  flags=(); [[ $kind == tls-fail ]] && flags=(-DFAIL_ATTACH)
  "$cc" -O1 "${flags[@]}" -c tests/lifecycle-tls.c -o "out-tls/tests/$kind.obj"
  "$link" /dll /nodefaultlib /entry:test_dll_entry /include:_tls_used /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat "/out:out-tls/tests/$kind.dll" "out-tls/tests/$kind.obj" "$kernel32"
done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static tests/mapper-lifecycle.cpp -o out-tls/tests/mapper-lifecycle.exe
if [[ ${1:-} == --build-only ]]; then exit 0; fi
# Do not pass unrelated credentials to Wine child processes.
env -u DP_PASS -u TS_AUTHKEY WINEDEBUG=-all wine out-tls/tests/mapper-lifecycle.exe 'out-tls/tests/lifecycle.dll' 'out-tls/tests/lifecycle-fail.dll' 'out-tls/tests/tls.dll' 'out-tls/tests/tls-fail.dll'
