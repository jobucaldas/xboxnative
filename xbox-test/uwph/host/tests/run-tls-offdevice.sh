#!/usr/bin/env bash
# OFF-DEVICE only. Wine 10 lacks VirtualProtectFromApp; harness aliases desktop
# memory APIs. Console smoke must independently prove the actual FromApp calls.
set -euo pipefail
cd "$(dirname "$0")/.."
bash tests/build-tls-guests.sh out-tls
UWP_OBJ_DIR=out-tls/offdevice-obj /workspace/ref/uwp-crossbuild/scripts/build.sh --out out-tls/tests/tls-host-ms.exe tests/tls-host.cpp -- /MT /O1 /EHsc
# Never pass unrelated service credentials to guest execution.
env -u DP_PASS -u TS_AUTHKEY WINEDEBUG=-all wine out-tls/tests/tls-host-ms.exe | tee out-tls/tests/tls-offdevice.log
grep -F 'WXL-TLS-OK: TLS value correct on two threads, callback ran, exception caught' out-tls/tests/tls-offdevice.log
test "$(grep -c 'WXL CXX mapped DLL destructor ran' out-tls/tests/tls-offdevice.log)" -eq 2
test "$(grep -c 'WXL CXX mapped EXE caught 73' out-tls/tests/tls-offdevice.log)" -eq 2
! grep -E 'FAIL|new=0x(40|80) ' out-tls/tests/tls-offdevice.log
