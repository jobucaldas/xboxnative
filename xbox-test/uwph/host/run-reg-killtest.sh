#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Kill only this test's process group, then verify crash consistency under Wine.
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
cd "$(dirname "$0")"
if [ "$#" -gt 2 ]; then printf 'usage: %s [OUTDIR [CYCLES]]\n' "$0" >&2; exit 2; fi
o=${1:-out/reg-selftest}; cycles=${2:-8}
[[ "$cycles" =~ ^[1-9][0-9]*$ ]] || { printf 'CYCLES must be positive\n' >&2; exit 2; }
o=$(cd "$o" && pwd)
[ -f "$o/selftest.exe" ] || { printf 'Run run-reg-selftest.sh first\n' >&2; exit 1; }
command -v setsid >/dev/null
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$o/wine-prefix}
d=$(mktemp -d "$o/kill-ls.XXXXXXXX"); w=$(winepath -w "$d"); bad=0; pid=
cleanup() {
  if [ -n "$pid" ]; then
    kill -KILL -- "-$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
: > "$o/kill-summary.txt"
for ((i=1; i<=cycles; i++)); do
  setsid wine "$o/selftest.exe" "$w" stress 600 >"$o/kill-stress-$i.log" 2>&1 & pid=$!
  sleep $(( 2 + RANDOM % 4 ))
  # A writer which exited before the crash point is a failure, not crash proof.
  if ! kill -KILL -- "-$pid" 2>/dev/null; then
    printf 'KILLTEST writer exited before crash cycle=%s\n' "$i" >&2
    bad=1
  fi
  wait "$pid" 2>/dev/null || true
  pid=
  sleep 1
  if ! wine "$o/selftest.exe" "$w" verify 2>&1 | tee "$o/kill-verify-$i.log"; then bad=1; fi
  if ! grep -F 'REGSTRESS-VERIFY OK' "$o/kill-verify-$i.log" | tee -a "$o/kill-summary.txt"; then bad=1; fi
done
if [ "$bad" = 0 ]; then printf 'KILLTEST PASS cycles=%s\n' "$cycles"; else printf 'KILLTEST FAIL\n'; fi
exit "$bad"
