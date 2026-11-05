#!/bin/bash
# wait-log.sh PackageFullName FILE PATTERN [TIMEOUT_S=120] [OUT=/tmp/wait-log.txt] -- poll \LocalState\FILE until regex matches
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/dp-lib.sh"
pkg="$1"; f="$2"; pat="$3"; to="${4:-120}"; out="${5:-/tmp/wait-log.txt}"; end=$((SECONDS+to))
# Logs can be truncated/replaced on relaunch. Never resume against an older
# local log: a Range-at-EOF response would otherwise accept stale results.
tmp=$(mktemp); trap 'rm -f "$tmp"' EXIT
while [ $SECONDS -lt $end ]; do
  rm -f "$tmp"
  if "$HERE/get-file.sh" "$pkg" '\LocalState' "$f" "$tmp" >/dev/null 2>&1; then
    mv "$tmp" "$out"
    grep -Eq "$pat" "$out" && { echo "MATCH in $out"; exit 0; }
  fi
  sleep 3
done; echo "TIMEOUT"; exit 1
