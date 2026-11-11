#!/bin/bash
# play-test.sh <appid> [TIMEOUT_S=600] [OUTDIR]
# THE ACCEPTANCE TEST: "Play <game> via the launcher". Runs the real launcher path on the console:
# open detail -> press main button (Install when needed, then Play after the install finishes) -> wait for 'launch: RESULT'
# -> collect screenshots + boot.log + launch_probe.log + launch_request/result.json into OUTDIR.
# The launcher never launches by itself after an install: when that first press starts a download this
# script waits for it to finish and presses the button again, exactly as the player does.
# Input is SYNTHETIC: one-shot LocalState\cmd.txt lines (consumed by the app, logged as "cmd.txt SYNTHETIC command").
# Env: PKG=<PackageFullName> (default: auto-detect Xboxnative.Launcher), PT_MAXMB=<n> (size guard via dlmax.txt: refuse download if
#      bigger than n MB; 0 = dry run that only prints the manifest size), PT_NOKILL=1 (don't restart the app first).
# Exit: 0 = fresh, clean guest completion plus GAP summary (visual/playable sign-off still required);
#       1 = runtime/install failure, nonterminal progress, stale evidence, timeout, missing GAP or a Device Portal
#       transport error (any helper that cannot report its HTTP status fails the run; it never counts as "not yet").
# Never touches steam_session.bin. DP_PASS comes from the env and is never printed. Needs PATH with $HOME/.local/bin.
HERE="$(cd "$(dirname "$0")" && pwd)"; DP="$HERE/../dp"; . "$DP/dp-lib.sh"
app="$1"; to="${2:-600}"; out="${3:-out/runs/play-$1-$(date +%Y%m%d-%H%M%S)}"
[[ "$app" =~ ^[0-9]+$ ]] || { echo "usage: $0 <appid> [timeout_s] [outdir]"; exit 2; }
mkdir -p "$out"; tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
full="${PKG:-}"
if [ -z "$full" ]; then
  full=$(dp_lock curl_dp /api/app/packagemanager/packages | grep -o 'Xboxnative\.Launcher_[0-9.]*_x64__[a-z0-9]*' | sort -u | tail -1)
fi
[ -n "$full" ] || { echo "no Xboxnative.Launcher package found"; exit 2; }
echo "package: $full  app: $app  out: $out"
tfail() { echo "TRANSPORT FAIL: $1 (no acceptance claimed; see $out)"; exit 1; }
# dp_step LABEL HELPER ARGS...: helper must exit 0 with a 2xx "<op> HTTP nnn" status line.
dp_step() {
  local label="$1" r; shift
  r=$(bash "$@" 2>/dev/null) || tfail "$label (${r##*$'\n'})"
  r=${r##*$'\n'}; [[ $r =~ \ HTTP\ 2[0-9][0-9]($|[[:space:]]) ]] || tfail "$label (${r:-no response})"
  echo "$r"
}
cmd() { printf '%s\n' "$@" > "$tmp/cmd.txt"; dp_step "upload cmd.txt '$*'" "$DP/put-file.sh" "$full" '\LocalState' "$tmp/cmd.txt"; }
pull() {
  # Logs can be truncated or rewritten by a new app session. Resuming an old
  # local snapshot preserves stale bytes, including a previous launch result.
  # Returns 0 = fetched, 2 = absent (HTTP 404), 1 = transport error. A failed
  # fetch never leaves an error body or partial bytes behind as evidence.
  local r; rm -f "$2"
  r=$(bash "$DP/get-file.sh" "$full" '\LocalState' "$1" "$2" 2>/dev/null) && return 0
  rm -f "$2"; [[ $r == *"get HTTP 404 "* ]] && return 2; return 1
}
need() { pull "$1" "$2" || tfail "download $1"; }                # must exist and be readable
evidence() { pull "$1" "$2"; [ $? -ne 1 ] || tfail "download $1"; } # absent is judged by the checks below
attempt_of() { python3 -I -c 'import json,sys
try: v=json.load(open(sys.argv[1])).get("attemptId")
except Exception: v=None
print(v if isinstance(v,str) else "")' "$1" 2>/dev/null; }
# The previous attempt identity, so evidence left by an earlier run (e.g. after a
# refused stop or a launch that never happened) can never satisfy this one.
pull launch_request.json "$tmp/prev_request.json"; rc=$?
[ $rc -ne 1 ] || tfail "download previous launch_request.json"
stale=""; [ $rc -eq 0 ] && stale=$(attempt_of "$tmp/prev_request.json")
shot() { bash "$DP/screenshot.sh" "$out/$1.png" >/dev/null 2>&1 && echo "shot $1.png"; }
if [ -z "$PT_NOKILL" ]; then
  # A non-2xx answer is tolerated (nothing may be running); no answer at all is not.
  r=$(bash "$DP/kill.sh" "$full" 2>/dev/null) && [[ ${r##*$'\n'} =~ \ HTTP\ [1-5][0-9][0-9]($|[[:space:]]) ]] || tfail "stop request (${r##*$'\n'})"
  sleep 3
fi
: > "$tmp/cmd.txt"; dp_step "clear cmd.txt" "$DP/put-file.sh" "$full" '\LocalState' "$tmp/cmd.txt"   # overwrite any stale cmd.txt with an empty (harmless) one
n0=0   # boot.log is truncated on every app start; with PT_NOKILL we count from the current length
if [ -n "$PT_NOKILL" ]; then evidence boot.log "$tmp/before.log"; n0=$(wc -l < "$tmp/before.log" 2>/dev/null || echo 0); fi
if [ -n "$PT_MAXMB" ]; then printf '%s\n' "$PT_MAXMB" > "$tmp/dlmax.txt"; dp_step "upload dlmax.txt" "$DP/put-file.sh" "$full" '\LocalState' "$tmp/dlmax.txt"; fi
# Family name = <Name>_<publisher hash>, from the detected full name.
dp_step "launch app" "$DP/launch.sh" "${full%%_*}_${full##*__}!App" "$full"
ok=1; for k in $(seq 1 40); do evidence boot.log "$tmp/b.log"; tail -n +$((n0+1)) "$tmp/b.log" | grep -Eq 'library(\(cm\))?: owned=' && { ok=0; break; }; sleep 4; done
[ $ok -eq 0 ] || { echo "library never loaded"; cp "$tmp/b.log" "$out/boot.log" 2>/dev/null; exit 1; }
sleep 5
cmd "open $app"; sleep 8; shot 1-detail
end=$((SECONDS+to)); i=0; res=1; attempt=""; pressed=0; press_mark=0
while [ $SECONDS -lt $end ]; do
  need boot.log "$tmp/b.log"
  session=$(tail -n +$((n0+1)) "$tmp/b.log")
  after=$(printf '%s\n' "$session" | tail -n +$((press_mark + 1)))
  # The launcher must never launch by itself: a launch request that follows a
  # finished download with no press in between is exactly that, so fail the run.
  if [ "$pressed" -eq 1 ] && printf '%s\n' "$after" | grep -q "dl: ===== DONE appid $app " && printf '%s\n' "$after" | grep -q 'launch: request written'; then
    echo "AUTO-LAUNCHED after install: the launcher must not launch by itself"; break
  fi
  # Only the newest request written in this session, and never the previous run's, is ours.
  a=$(printf '%s\n' "$session" | sed -nE 's/.*launch: request written .* attempt=([0-9]+:[0-9]+)([[:space:]].*)?$/\1/p' | tail -1)
  [ -n "$a" ] && [ "$a" != "$stale" ] && attempt=$a
  if [ -n "$attempt" ] && printf '%s\n' "$session" | grep -Eq "launch: RESULT attempt=$attempt( |\$)"; then res=0; break; fi
  if printf '%s\n' "$session" | grep -Eq 'dl: (FAILED|failed)|Test guard|Cannot reach Steam'; then echo "install did not complete"; break; fi
  # The launcher never launches after an install. Press the main button once (Install,
  # or Play when it is already installed), then again once the download this press
  # started is done. A download already running (PT_NOKILL kept the session) is
  # waited for, never cancelled by a stray press.
  dl_last=$(printf '%s\n' "$session" | grep -E "dl: ===== start appid $app |dl: ===== DONE appid $app " | tail -1)
  if [ "$pressed" -eq 0 ]; then
    case "$dl_last" in
      *"start appid $app "*) : ;;   # pressing A now would cancel that download
      *) press_mark=$(printf '%s\n' "$session" | wc -l); cmd "press"; pressed=1; sleep 1; shot 2-after-press ;;
    esac
  elif [ "$pressed" -eq 1 ] && printf '%s\n' "$after" | grep -q "dl: ===== DONE appid $app "; then
    press_mark=$(printf '%s\n' "$session" | wc -l); sleep 2; cmd "press"; pressed=2; sleep 1; shot 3-installed-press
  fi
  i=$((i+1)); { [ $((i%6)) -eq 0 ] || [ $i -le 12 ]; } && shot "3-wait-$(printf %03d $i)"; sleep 1
done
sleep 4; shot 4-final
need boot.log "$out/boot.log"; evidence launch_probe.log "$out/launch_probe.log"
if [[ "$app" == 480 || "$app" == 48000 ]]; then
  evidence "pe32-imports-$app.csv" "$out/pe32-imports-$app.csv"
fi
evidence launch_request.json "$out/launch_request.json"; evidence launch_result.json "$out/launch_result.json"
tail -n +$((n0+1)) "$out/boot.log" > "$out/boot.session.log"
echo "---- synthetic commands / store / launch lines ----"
grep -E 'SYNTHETIC|store:|dl:|launch|GAP:' "$out/boot.session.log" | tail -30
gap_ok=0
grep -E '^GAP: total=[0-9]+ missing=[0-9]+ win32u=[0-9]+$' "$out/launch_probe.log" && gap_ok=1
if [[ "$app" == 480 || "$app" == 48000 ]]; then
  grep -E '^PE32 mapped, imports=[0-9]+, gap=[0-9]+; .*reloc-errors=0;' "$out/launch_probe.log" || { echo "NO PE32 MAPPING"; exit 1; }
  grep -q '^i386,' "$out/pe32-imports-$app.csv" || { echo "NO I386 IMPORT LIST"; exit 1; }
fi
echo "---- fps ----"; grep -E 'fps~' "$out/boot.session.log" | tail -2
if [ $res -eq 0 ]; then
  fresh=$(attempt_of "$out/launch_request.json")
  [ -n "$fresh" ] && [ "$fresh" = "$attempt" ] && [ "$fresh" != "$stale" ] ||
    { echo "STALE OR UNCORRELATED REQUEST: launch_request.json is not this run's attempt (see $out)"; exit 1; }
fi
[ $res -eq 0 ] && { [ "$gap_ok" -eq 1 ] || { echo "NO GAP SUMMARY (see $out/launch_probe.log)"; exit 1; }; python3 -I "$HERE/check-play-result.py" "$app" "$out" || exit 1; echo "GUEST COMPLETED: $(grep -E "launch: RESULT attempt=$attempt( |\$)" "$out/boot.session.log" | tail -1)"; echo "evidence: $out (visual/playable sign-off still required)"; exit 0; }
echo "NO LAUNCH RESULT (see $out)"; exit 1
