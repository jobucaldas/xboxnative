#!/usr/bin/env bash
# OFF-DEVICE Wine check, not Xbox evidence. No console traffic.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out-tls/tests
x86_64-w64-mingw32-gcc -O1 -c tests/lifecycle-dll.c -o out-tls/tests/lifecycle.obj
lld-link-19 /dll /nodefaultlib /entry:test_dll_entry /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:out-tls/tests/lifecycle.dll out-tls/tests/lifecycle.obj /usr/x86_64-w64-mingw32/lib/libkernel32.a
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static tests/mapper-lifecycle.cpp -o out-tls/tests/mapper-lifecycle.exe
# Do not pass unrelated credentials to Wine child processes.
env -u DP_PASS -u TS_AUTHKEY WINEDEBUG=-all wine out-tls/tests/mapper-lifecycle.exe 'out-tls/tests/lifecycle.dll'
