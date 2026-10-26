#!/bin/bash
# play-test.sh <appid> [TIMEOUT_S=600] [OUTDIR]
# THE ACCEPTANCE TEST: "Play <game> via the launcher". Runs the real launcher path on the console:
# open detail -> press main button (Install if needed, then auto-launch / Play) -> wait for 'launch: RESULT'
# -> collect screenshots + boot.log + launch_probe.log + launch_request/result.json into OUTDIR.
# Input is SYNTHETIC: one-shot LocalState\cmd.txt lines (consumed by the app, logged as "cmd.txt SYNTHETIC command").
# Env: PKG=<PackageFullName> (default: auto-detect Grok.XboxTest), PT_MAXMB=<n> (size guard via dlmax.txt: refuse download if
#      bigger than n MB; 0 = dry run that only prints the manifest size), PT_NOKILL=1 (don't restart the app first).
# Exit: 0 = a launch attempt and GAP summary were reported; 1 = install failed/timeout/missing GAP; 2 = usage/setup.
# Never touches steam_session.bin. DP_PASS comes from the env and is never printed. Needs PATH with $HOME/.local/bin.
HERE="$(cd "$(dirname "$0")" && pwd)"; DP="$HERE/../dp"; . "$DP/dp-lib.sh"
app="$1"; to="${2:-600}"; out="${3:-/workspace/x64-run/runs/play-$1-$(date +%Y%m%d-%H%M%S)}"
[[ "$app" =~ ^[0-9]+$ ]] || { echo "usage: $0 <appid> [timeout_s] [outdir]"; exit 2; }
mkdir -p "$out"; tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
full="${PKG:-}"
if [ -z "$full" ]; then
  full=$(dp_lock curl_dp /api/app/packagemanager/packages | grep -o 'Grok\.XboxTest_[0-9.]*_x64__[a-z0-9]*' | sort -u | tail -1)
fi
[ -n "$full" ] || { echo "no Grok.XboxTest package found"; exit 2; }
echo "package: $full  app: $app  out: $out"
cmd() { printf '%s\n' "$@" > "$tmp/cmd.txt"; bash "$DP/put-file.sh" "$full" '\LocalState' "$tmp/cmd.txt" | tail -1; }
pull() { bash "$DP/get-file.sh" "$full" '\LocalState' "$1" "$2" >/dev/null 2>&1; }
shot() { bash "$DP/screenshot.sh" "$out/$1.png" >/dev/null 2>&1 && echo "shot $1.png"; }
if [ -z "$PT_NOKILL" ]; then bash "$DP/kill.sh" "$full" >/dev/null; sleep 3; fi
: > "$tmp/cmd.txt"; bash "$DP/put-file.sh" "$full" '\LocalState' "$tmp/cmd.txt" | tail -1   # overwrite any stale cmd.txt with an empty (harmless) one
n0=0   # boot.log is truncated on every app start; with PT_NOKILL we count from the current length
if [ -n "$PT_NOKILL" ]; then pull boot.log "$tmp/before.log"; n0=$(wc -l < "$tmp/before.log" 2>/dev/null || echo 0); fi
if [ -n "$PT_MAXMB" ]; then printf '%s\n' "$PT_MAXMB" > "$tmp/dlmax.txt"; bash "$DP/put-file.sh" "$full" '\LocalState' "$tmp/dlmax.txt" | tail -1; fi
bash "$DP/launch.sh" 'Grok.XboxTest_dk715arakrne0!App' "$full" | tail -1
ok=1; for k in $(seq 1 40); do pull boot.log "$tmp/b.log"; tail -n +$((n0+1)) "$tmp/b.log" | grep -Eq 'library(\(cm\))?: owned=' && { ok=0; break; }; sleep 4; done
[ $ok -eq 0 ] || { echo "library never loaded"; cp "$tmp/b.log" "$out/boot.log" 2>/dev/null; exit 1; }
sleep 5
cmd "open $app"; sleep 8; shot 1-detail
cmd "press"; sleep 1; shot 2-after-press
end=$((SECONDS+to)); i=0; res=1
while [ $SECONDS -lt $end ]; do
  pull boot.log "$tmp/b.log"
  if tail -n +$((n0+1)) "$tmp/b.log" | grep -q 'launch: RESULT'; then res=0; break; fi
  if tail -n +$((n0+1)) "$tmp/b.log" | grep -Eq 'dl: (FAILED|failed)|Test guard|Cannot reach Steam'; then echo "install did not complete"; break; fi
  i=$((i+1)); { [ $((i%6)) -eq 0 ] || [ $i -le 12 ]; } && shot "3-wait-$(printf %03d $i)"; sleep 1
done
sleep 4; shot 4-final
pull boot.log "$out/boot.log"; pull launch_probe.log "$out/launch_probe.log"
if [[ "$app" == 480 || "$app" == 48000 ]]; then
  pull "pe32-imports-$app.csv" "$out/pe32-imports-$app.csv"
fi
pull launch_request.json "$out/launch_request.json"; pull launch_result.json "$out/launch_result.json"
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
[ $res -eq 0 ] && { [ "$gap_ok" -eq 1 ] || { echo "NO GAP SUMMARY (see $out/launch_probe.log)"; exit 1; }; echo "LAUNCH ATTEMPTED: $(grep 'launch: RESULT' "$out/boot.session.log" | tail -1)"; echo "evidence: $out"; exit 0; }
echo "NO LAUNCH RESULT (see $out)"; exit 1
