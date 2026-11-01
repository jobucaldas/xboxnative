#!/usr/bin/env bash
# Off-device kill-mid-write consistency test under Wine: run the stress writer, kill -9 it at random points, verify the db each time.
set -uo pipefail
export PATH="$HOME/.local/bin:$PATH"
cd "$(dirname "$0")"
o=${1:-out/reg-selftest}; cycles=${2:-8}; export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine}
d="$o/kill-ls"; rm -rf "$d"; mkdir -p "$d"; w=$(winepath -w "$d"); bad=0
for i in $(seq 1 "$cycles"); do
  wine "$o/selftest.exe" "$w" stress 600 >"$o/kill-stress-$i.log" 2>&1 & pid=$!
  sleep $(( 2 + RANDOM % 4 )); pkill -9 -f "selftest.exe" ; wait "$pid" 2>/dev/null; sleep 1
  wine "$o/selftest.exe" "$w" verify 2>&1 | tee "$o/kill-verify-$i.log" | grep -E 'REGSTRESS-VERIFY' | tail -1 | tee -a "$o/kill-summary.txt"
  grep -q 'REGSTRESS-VERIFY OK' "$o/kill-verify-$i.log" || bad=1
done
[ $bad = 0 ] && echo "KILLTEST PASS cycles=$cycles" || echo "KILLTEST FAIL"
exit $bad
