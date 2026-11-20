#!/bin/bash
# play-sweep.sh [--file APPIDS.txt | --installed | APPID ...] [options]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
DP="$HERE/../dp"
PARSER="$HERE/play-sweep-parser.py"
usage() {
  cat <<'EOF'
Usage: play-sweep.sh (--installed | --file APPIDS.txt | APPID...) [options]

Run owned installed games sequentially through play-test.sh and write a CSV.
  --installed       Read the launcher's dl_<appid>.done install markers
  --file FILE       Read appids, one per line (# comments allowed)
  --timeout SECS    Bound each play-test run (default: 600)
  --outdir DIR      Store the CSV and redacted per-game excerpts there
  --allow-install MB  Let play-test download a missing game up to MB megabytes
                    (default: no downloads; a game that is not installed is
                    reported as blocked at its install prompt)
  --dry-run         Print the plan without contacting the console
  -h, --help        Show this help

The runner refuses to start over an active guest, uses the existing Device
Portal helpers/lock, sends gquit after each attempt, and never uninstalls games.
EOF
}
source_kind=""; source_file=""; timeout_s=600; maxmb=0; outdir=""; dry=0; declare -a apps=()
while (($#)); do
  case "$1" in
    --installed) [[ -z "$source_kind" ]] || { echo "choose one input source" >&2; exit 2; }; source_kind=installed; shift ;;
    --file) [[ -z "$source_kind" && $# -ge 2 ]] || { echo "choose one input source and provide a file" >&2; exit 2; }; source_kind=file; source_file=$2; shift 2 ;;
    --timeout) [[ $# -ge 2 && "$2" =~ ^[0-9]+$ && $2 -ge 1 && $2 -le 3600 ]] || { echo "--timeout must be 1..3600 seconds" >&2; exit 2; }; timeout_s=$2; shift 2 ;;
    --outdir) [[ $# -ge 2 && -n "$2" ]] || { echo "--outdir needs a directory" >&2; exit 2; }; outdir=$2; shift 2 ;;
    --allow-install) [[ $# -ge 2 && "$2" =~ ^[0-9]+$ && $2 -ge 1 ]] || { echo "--allow-install needs a size in MB (>= 1)" >&2; exit 2; }; maxmb=$2; shift 2 ;;
    --dry-run) dry=1; shift ;;
    -h|--help) usage; exit 0 ;;
    --*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    *) [[ -z "$source_kind" || "$source_kind" == args ]] || { echo "cannot mix input sources" >&2; exit 2; }; source_kind=args; apps+=("$1"); shift ;;
  esac
done
[[ -n "$source_kind" ]] || { usage >&2; exit 2; }
if [[ "$source_kind" == file ]]; then
  [[ -r "$source_file" ]] || { echo "cannot read appid file" >&2; exit 2; }
  while IFS= read -r line || [[ -n "$line" ]]; do
    line=${line%%#*}; line=${line//[[:space:]]/}
    [[ -n "$line" ]] && apps+=("$line")
  done < "$source_file"
elif [[ "$source_kind" == args ]]; then
  :
fi
if [[ "$source_kind" == installed && $dry -eq 0 ]]; then
  # The launcher uses these same LocalState install markers for its Installed view.
  . "$DP/dp-lib.sh"
  full=$(bash "$DP/packages-json.sh" | grep -o 'Xboxnative\.Launcher_[0-9.]*_x64__[a-z0-9]*' | sort -u | tail -1)
  [[ -n "$full" ]] || { echo "no Xboxnative.Launcher package found" >&2; exit 1; }
  entries=$(bash "$DP/ls-dir.sh" "$full" '\LocalState')
  while IFS= read -r line; do
    if [[ "$line" =~ ^[^[:space:]]+[[:space:]]+[^[:space:]]+[[:space:]]dl_([0-9]+)\.done$ ]]; then apps+=("${BASH_REMATCH[1]}"); fi
  done <<< "$entries"
fi
[[ ${#apps[@]} -gt 0 || "$source_kind" == installed && $dry -eq 1 ]] || { echo "no appids found" >&2; exit 2; }
declare -A seen=()
unique=()
for app in "${apps[@]}"; do
  [[ "$app" =~ ^[0-9]+$ ]] || { echo "invalid appid: $app" >&2; exit 2; }
  [[ -z ${seen[$app]:-} ]] || continue
  seen[$app]=1; unique+=("$app")
done
if [[ "$source_kind" == installed && $dry -eq 1 ]]; then
  printf 'Installed marker discovery: launcher LocalState dl_<appid>.done files (deferred; dry-run made no console requests)\n'
fi
if [[ $dry -eq 1 ]]; then
  if [[ $maxmb -eq 0 ]]; then install="no downloads"; else install="downloads up to ${maxmb} MB"; fi
  for app in "${unique[@]}"; do printf 'would run appid %s with timeout %ss (%s), then write gquit; no uninstall\n' "$app" "$timeout_s" "$install"; done
  exit 0
fi
command -v timeout >/dev/null || { echo "timeout command is required" >&2; exit 2; }
# Load the shared auth/lock helpers once. Every helper invocation below retains its own
# per-operation flock, credentials and CSRF handling.
. "$DP/dp-lib.sh"
full=$(bash "$DP/packages-json.sh" | grep -o 'Xboxnative\.Launcher_[0-9.]*_x64__[a-z0-9]*' | sort -u | tail -1)
[[ -n "$full" ]] || { echo "no Xboxnative.Launcher package found" >&2; exit 1; }
if [[ -z "$outdir" ]]; then outdir="$ROOT/out/sweeps/play-sweep-$(date +%Y%m%d-%H%M%S)"; fi
mkdir -p "$outdir"
csv="$outdir/results.csv"
printf 'appid,name,arch,terminal_result,exit_code,first_failure,gap_summary,frames_reached,elapsed\n' > "$csv"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/play-sweep.XXXXXX")
capped=0
# The launcher reads dlmax.txt only when a download starts, so a sweep that never downloads
# leaves its cap (0) behind. On exit, overwrite it with -1, which the launcher treats as no cap.
clear_cap() {
  [[ $capped -eq 1 ]] || return 0
  mkdir -p "$tmp/cap"; printf '%s\n' -1 > "$tmp/cap/dlmax.txt"
  bash "$DP/put-file.sh" "$full" '\LocalState' "$tmp/cap/dlmax.txt" >/dev/null 2>&1 || true
}
trap 'clear_cap; rm -rf "$tmp"' EXIT
active_guest() {
  local logfile=$1
  python3 -I - "$logfile" <<'PY'
import re,sys
s=open(sys.argv[1],errors="replace").read()
runs=list(re.finditer(r"launch: RUNNING attempt=([^\s]+)",s))
if not runs: raise SystemExit(1)
latest=runs[-1]
a=latest.group(1)
ended=any(m.start()>latest.start() for m in re.finditer(r"launch: RESULT attempt="+re.escape(a)+r"(?:\s|$)",s))
if ended: raise SystemExit(1)
print(a)
PY
}
preflight() {
  local running log="$tmp/preflight.log"
  running=$(bash "$DP/ps.sh" 2>/dev/null) || { echo "cannot verify running apps; refusing sweep" >&2; return 1; }
  if grep -Fq "pkg=$full" <<< "$running"; then
    rm -f "$log"
    bash "$DP/get-file.sh" "$full" '\LocalState' boot.log "$log" >/dev/null 2>&1 || { echo "cannot inspect launcher guest state; refusing sweep" >&2; return 1; }
    if active=$(active_guest "$log"); then echo "a guest is already running (attempt $active); refusing to start a sweep" >&2; return 1; fi
  fi
}
for app in "${unique[@]}"; do
  preflight || exit 1
  raw="$tmp/$app"; mkdir -p "$raw"
  start=$SECONDS
  capped=1
  set +e
  # PT_MAXMB=0 makes play-test refuse any download, so a sweep never installs a game
  # unless --allow-install asked for it.
  PT_MAXMB=$maxmb timeout --signal=TERM --kill-after=15s "${timeout_s}s" bash "$HERE/play-test.sh" "$app" "$timeout_s" "$raw" >"$tmp/play-test.out" 2>&1
  test_rc=$?
  set -e
  elapsed=$((SECONDS-start))

  # Ask the guest to close over the launcher cmd.txt channel. This is harmless if
  # play-test already observed a terminal result; it never uses force-stop.
  printf 'gquit\n' > "$tmp/cmd.txt"
  # put-file.sh exits 0 for any HTTP answer, so only a 2xx status line proves the quit was accepted.
  gquit_reply=$(bash "$DP/put-file.sh" "$full" '\LocalState' "$tmp/cmd.txt" 2>/dev/null) || gquit_reply=""
  [[ $gquit_reply =~ \ HTTP\ 2[0-9][0-9]($|[[:space:]]) ]] || { echo "could not send gquit for $app; stopping sweep" >&2; exit 1; }
  fetch_logs() {
    rm -f "$raw/boot.log" "$raw/launch_probe.log" "$raw/launch_request.json" "$raw/launch_result.json"
    for file in boot.log launch_probe.log launch_request.json launch_result.json; do
      # get-file.sh keeps the error body on a non-2xx answer; drop it so it is never read as evidence.
      bash "$DP/get-file.sh" "$full" '\LocalState' "$file" "$raw/$file" >/dev/null 2>&1 || rm -f "$raw/$file"
    done
  }
  [[ -s "$raw/boot.log" ]] || fetch_logs
  [[ -s "$raw/boot.log" ]] || { echo "cannot verify guest shutdown for $app; stopping sweep" >&2; exit 1; }
  if active=$(active_guest "$raw/boot.log" 2>/dev/null); then
    pattern="launch: RESULT attempt=$active([[:space:]]|$)"
    bash "$DP/wait-log.sh" "$full" boot.log "$pattern" 90 "$tmp/gquit.log" >/dev/null || { echo "guest $app did not report a clean quit; stopping sweep" >&2; exit 1; }
    fetch_logs
    if active=$(active_guest "$raw/boot.log" 2>/dev/null); then echo "guest $app did not quit cleanly; stopping sweep" >&2; exit 1; fi
  fi
  python3 -I "$PARSER" "$app" "$raw" --elapsed "$elapsed" --redacted-out "$outdir/$app.log" >> "$csv"
  printf 'appid %s: play-test exit %s; results in %s\n' "$app" "$test_rc" "$csv"
  # Do not advance while the previous guest remains active; there is no force-quit fallback.
  if active_guest "$raw/boot.log" 2>/dev/null; then echo "guest $app did not quit cleanly; stopping sweep" >&2; exit 1; fi
done
printf 'sweep complete: %s\n' "$csv"
