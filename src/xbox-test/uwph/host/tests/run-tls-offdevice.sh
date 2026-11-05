#!/usr/bin/env bash
# OFF-DEVICE only. Wine 10 lacks VirtualProtectFromApp; harness aliases desktop
# memory APIs. Console smoke must independently prove the actual FromApp calls.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out-tls/tests
bash tests/build-tls-guests.sh out-tls
UWP_OBJ_DIR=out-tls/offdevice-obj /workspace/ref/uwp-crossbuild/scripts/build.sh --out out-tls/tests/tls-host-ms.exe tests/tls-host.cpp -- /MT /O1 /EHsc
# Never pass unrelated service credentials to guest execution.
env -u DP_PASS -u TS_AUTHKEY WINEDEBUG=-all wine out-tls/tests/tls-host-ms.exe | tee out-tls/tests/tls-offdevice.log
grep -F 'WXL-TLS-OK: TLS value correct on two threads, callback ran, exception caught' out-tls/tests/tls-offdevice.log
test "$(grep -c 'WXL CXX mapped DLL destructor ran' out-tls/tests/tls-offdevice.log)" -eq 2
test "$(grep -c 'WXL CXX mapped EXE caught 73' out-tls/tests/tls-offdevice.log)" -eq 2
# Negated commands (`! cmd`) never trigger errexit; reject explicitly.
reject() { echo "TLS-OFFDEVICE FAIL: $*" >&2; exit 1; }
if grep -E 'FAIL|new=0x(40|80) ' out-tls/tests/tls-offdevice.log; then reject 'failure or RWX protection in clean run'; fi
grep -Fx 'TLS-RELEASE-ORDER consumer-first' <(tr -d '\r' < out-tls/tests/tls-offdevice.log) || reject 'dependency released before consumer'
# A successful guest check must not mask an explicit cleanup failure, even
# when the destructor's subsequent best-effort retry succeeds. Every mode also
# requires the dependency image to outlive its consumer; `wait` requires that
# nothing a possibly live worker uses is released.
for operation in tls-free unwind memory tls-read tls-read-persistent thread-read thread-clear wait; do
  expected=1; [[ $operation == tls-read-persistent ]] && expected=2
  log="out-tls/tests/tls-cleanup-$operation.log"
  env -u DP_PASS -u TS_AUTHKEY WINEDEBUG=-all wine out-tls/tests/tls-host-ms.exe "$operation" | tee "$log"
  tr -d '\r' < "$log" | grep -E "^TLS-CLEANUP-DETECTED operation=$operation injected=$expected " || reject "$operation not detected"
  if grep -F 'WXL-TLS-OK' "$log"; then reject "$operation reported success"; fi
done
echo 'TLS-OFFDEVICE PASS'
