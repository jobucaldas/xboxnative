#!/bin/bash
# game-pad.sh <buttons> [hold_ms] [--seq <name> <file>] [--wait S] [--pkg PackageFullName]
# Drive a RUNNING game's pad through the launcher's cmd.txt channel:
#   game-pad.sh DPAD_DOWN            press D-pad down (120 ms default hold)
#   game-pad.sh "A,B" 250            press A and B together for 250 ms
#   game-pad.sh --seq repro repro.txt  arm LocalState\gpadseq\repro.txt (uploaded from repro.txt)
# The command is written to LocalState\cmd.txt (one-shot, consumed on read)
# and the script waits for the ack line in LocalState\boot.log:
#   "cmd.txt: gpad armed" / "cmd.txt: gpadseq '<name>' armed"  (success)
#   "cmd.txt: gpad rejected" / "cmd.txt: gpadseq ... rejected" (syntax error)
#   "cmd.txt: gpad ignored, no game running"                   (no guest)
# Only works while a game guest is on screen (the launcher gates it there).
# Env: PKG=<PackageFullName> (default: auto-detect Xboxnative.Launcher), DP_PASS via dp-lib.sh.
# Never touches steam_session.bin; DP_PASS is never printed.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; DP="$HERE"; . "$DP/dp-lib.sh"
usage() { echo "usage: $0 <buttons> [hold_ms] | --seq <name> <file> [--wait seconds] [--pkg PackageFullName]" >&2; exit 2; }

buttons="" hold="" seq_name="" seq_file="" wait_s="30" full="${PKG:-}"
while [ $# -gt 0 ]; do
  case "$1" in
    --seq) [ $# -ge 3 ] || usage; seq_name="$2"; seq_file="$3"; shift 3 ;;
    --wait) [ $# -ge 2 ] || usage; wait_s="$2"; shift 2 ;;
    --pkg) [ $# -ge 2 ] || usage; full="$2"; shift 2 ;;
    *) if [ -z "$buttons" ]; then buttons="$1"; elif [ -z "$hold" ]; then hold="$1"; else usage; fi; shift ;;
  esac
done

if [ -n "$seq_name" ]; then
  [ -n "$buttons" ] || [ -n "$hold" ] && usage
  [ -f "$seq_file" ] || { echo "no such gpadseq file: $seq_file" >&2; exit 2; }
else
  [ -n "$buttons" ] || usage
fi

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
if [ -z "$full" ]; then
  full=$(dp_lock curl_dp /api/app/packagemanager/packages | grep -o 'Xboxnative\.Launcher_[0-9.]*_x64__[a-z0-9]*' | sort -u | tail -1)
fi
[ -n "$full" ] || { echo "no Xboxnative.Launcher package found"; exit 2; }
echo "package: $full"

if [ -n "$seq_name" ]; then
  # Upload the program first, then the one-shot command that arms it.
  "$DP/put-file.sh" "$full" '\LocalState\gpadseq' "$seq_file"
  printf 'gpadseq %s\n' "$seq_name" > "$tmp/cmd.txt"
  ack="cmd.txt: gpadseq '$seq_name' armed"
  nack="cmd.txt: gpadseq '$seq_name' rejected"
else
  printf 'gpad %s%s\n' "$buttons" "${hold:+ $hold}" > "$tmp/cmd.txt"
  ack="cmd.txt: gpad armed"
  nack="cmd.txt: gpad rejected"
fi
"$DP/put-file.sh" "$full" '\LocalState' "$tmp/cmd.txt"

# The launcher consumes cmd.txt on its next frame tick; boot.log carries the
# ack. Poll the log the way wait-log.sh does (it is re-created on relaunch).
if "$HERE/../../dp/wait-log.sh" "$full" boot.log "($ack)|($nack)|(gpad ignored, no game running)" "$wait_s" "$tmp/boot.log" >/dev/null 2>&1; then
  if grep -Eq "$nack" "$tmp/boot.log"; then
    echo "REJECTED: $(grep -E "$nack" "$tmp/boot.log" | tail -1)"; exit 3
  fi
  if grep -q "gpad ignored, no game running" "$tmp/boot.log"; then
    echo "IGNORED: no game running (gpad needs a guest on screen)"; exit 4
  fi
  echo "ACK: $(grep -E "$ack" "$tmp/boot.log" | tail -1)"
  exit 0
fi
echo "TIMEOUT waiting for the gpad ack in boot.log (${wait_s}s)"; exit 1
