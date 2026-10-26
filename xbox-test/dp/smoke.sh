#!/bin/bash
# smoke.sh RID FULL LOGFILE PATTERN [TIMEOUT_S=60] [SHOT.png=/tmp/smoke-shot.png] [MSIX] [CERT]
# One-command pipeline: (install if MSIX given) -> launch -> wait-log -> screenshot -> kill.
# Prints PASS/FAIL per step plus a summary line; exit code is the failure count.
# Example (already-installed Boxedwine DevMode probe app, verified live):
#   ./smoke.sh 'Joao.Boxedwine.DevMode_8j5c92gwe8f3c!App' \
#     Joao.Boxedwine.DevMode_0.1.6.0_x64__8j5c92gwe8f3c ladder-native.log 'corewindow-swapchain PASS'
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/dp-lib.sh"
rid="$1"; full="$2"; logfile="$3"; pattern="$4"; to="${5:-60}"; shot="${6:-/tmp/smoke-shot.png}"; msix="$7"; cer="$8"
[ -n "$rid" ] && [ -n "$full" ] && [ -n "$logfile" ] && [ -n "$pattern" ] || {
  echo "usage: $0 RID FULL LOGFILE PATTERN [TIMEOUT_S] [SHOT.png] [MSIX] [CERT]"; exit 2; }
ok=0; fail=0
step() {
  local name="$1"; shift
  echo "-- $name --"
  local out; out=$("$@" 2>&1); local rc=$?
  echo "$out"
  if [ $rc -eq 0 ] && ! echo "$out" | grep -qE 'HTTP [45][0-9][0-9]'; then
    echo "PASS: $name"; ok=$((ok+1))
  else
    echo "FAIL: $name (exit $rc)"; fail=$((fail+1))
  fi
}
[ -n "$msix" ] && step install "$HERE/install.sh" "$msix" "$cer"
step launch "$HERE/launch.sh" "$rid" "$full"
step wait-log "$HERE/wait-log.sh" "$full" "$logfile" "$pattern" "$to"
step screenshot "$HERE/screenshot.sh" "$shot"
echo "-- kill --"
"$HERE/kill.sh" "$full"
# Kill's HTTP status is unreliable (can report 400 on a successful terminate) -- confirm via ps.sh,
# retrying briefly since the process can take a couple of seconds to actually disappear.
gone=1
for i in 1 2 3 4 5; do
  sleep 2
  "$HERE/ps.sh" | grep -qF "$full" || { gone=0; break; }
done
if [ $gone -eq 0 ]; then
  echo "PASS: kill (confirmed gone via ps.sh)"; ok=$((ok+1))
else
  echo "FAIL: kill (still listed in ps.sh)"; fail=$((fail+1))
fi
echo "== SUMMARY: $ok passed, $fail failed =="
exit "$fail"
