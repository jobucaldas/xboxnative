#!/usr/bin/env bash
# uwph-spike-dispatch acceptance script. Three parts, all off-console, all
# answering the lane's question ("can Wine's unix-call boundary be satisfied
# by a pure-PE replacement with no ELF .so behind it?"):
#
#  1. dispatch_shim.c -- a from-scratch, self-contained, pure-PE
#     implementation of the whole protocol (own opcode table, own
#     dispatcher, own handlers). Proves the protocol shape itself is
#     pure-PE-able.
#  2. hook_real_ntdll.c -- hooks the REAL prebuilt
#     /usr/lib/x86_64-linux-gnu/wine/x86_64-windows/ntdll.dll's exported
#     __wine_unixlib_handle with an ordinary Windows-ABI handler at the
#     wrong assumption (slot 0). Expected to fail/misfire -- kept as
#     evidence of the first wrong hypothesis.
#  3. hook_real_ntdll2.c -- same hook, but at the correct opcode slot (7,
#     matching Wine 10.0 source dlls/ntdll/unix/loader.c's
#     unix_call_funcs[] order) and with the handler tagged
#     __attribute__((sysv_abi)) to match the real dispatcher's internal
#     calling convention. This is the literal "load a copy of ntdll.dll"
#     step from the lane prompt, and it succeeds.
#
# See LANE_NOTES.md for the full verdict and caveats.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$here"

echo "=== part 1: from-scratch pure-PE dispatch_shim ==="
x86_64-w64-mingw32-gcc -O2 -Wall -o dispatch_shim.exe dispatch_shim.c
out1="$(wine dispatch_shim.exe 2>/dev/null | tr -d '\r')"
echo "$out1"
echo "$out1" | grep -q "^SHIM-ANSWERED RtlGetSystemTimePrecise" || { echo "run.sh: part 1 missing SHIM-ANSWERED line" >&2; exit 1; }
echo "$out1" | grep -q "^PASS$" || { echo "run.sh: part 1 shim did not PASS (delta > 5s or dispatch failed)" >&2; exit 1; }
echo "run.sh: part 1 OK"

echo
echo "=== part 2: hook real ntdll.dll, wrong slot/ABI (expected to NOT redirect) ==="
x86_64-w64-mingw32-gcc -O0 -g -Wall -o hook_real_ntdll.exe hook_real_ntdll.c
out2="$(wine hook_real_ntdll.exe 2>/dev/null | tr -d '\r')"
echo "$out2"
echo "$out2" | grep -q "^HOOK-RESULT: our handler was NOT invoked" || { echo "run.sh: part 2 unexpected result" >&2; exit 1; }
echo "run.sh: part 2 OK (confirms naive slot/ABI guess fails, as documented)"

echo
echo "=== part 3: hook real ntdll.dll, correct slot + sysv_abi (expected SUCCESS) ==="
x86_64-w64-mingw32-gcc -O0 -g -Wall -o hook_real_ntdll2.exe hook_real_ntdll2.c
out3="$(wine hook_real_ntdll2.exe 2>/dev/null | tr -d '\r')"
echo "$out3"
echo "$out3" | grep -q "^HOOK-RESULT: SUCCESS" || { echo "run.sh: part 3 did not succeed" >&2; exit 1; }
echo "run.sh: part 3 OK"

echo
echo "SHIM-ANSWERED NtQuerySystemTime-equivalent (RtlGetSystemTimePrecise) via pure-PE handler in all 3 parts where expected"
echo "=== HID/SETUPAPI private backend (off-device Wine) ==="
bash "$here/../hid/run.sh"
echo "run.sh: ALL OK"
