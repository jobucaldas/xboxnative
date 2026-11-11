#!/usr/bin/env bash
# Console proof for the SQLite registry (Xboxnative.Reg): fresh create, persistence across relaunch, one-time
# registry.json import, kill-mid-write consistency. One short window under CONSOLE.lock. REG_INSTALL=0 skips (re)install.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../../.." && pwd); cd "$root"; dp="$root/src/xbox-test/dp"
runs=${REG_RUNS:-/workspace/x64-run/runs/regsql}; mkdir -p "$runs"
seed=${REG_SEED:-/workspace/x64-run/runs/reg/evidence/registry-after-run2.json}
ver=$(sed -n 's/.*Name="Xboxnative.Reg".*Version="\([0-9.]*\)".*/\1/p' src/xbox-test/uwph/host/AppxManifest.Reg.xml)
full=Xboxnative.Reg_${ver}_x64__0bdeywx3x8szp; rid='Xboxnative.Reg_0bdeywx3x8szp!App'; cycles=${REG_KILL_CYCLES:-4}
exec 9>/workspace/x64-run/CONSOLE.lock; flock -w 900 9; export DP_LOCK_FD=9
put() { bash "$dp/put-file.sh" "$full" '\LocalState' "$1" | tail -1; }
mark() { printf '%s\n' "${2:-1}" > "$runs/$1"; put "$runs/$1"; }
launch() { bash "$dp/kill.sh" "$full" >/dev/null || true; printf 'REG awaiting fresh activation\n' > "$runs/k32.log"; put "$runs/k32.log" >/dev/null; bash "$dp/launch.sh" "$rid" "$full" | tail -1; }
[[ ${REG_INSTALL:-1} == 1 ]] && bash "$dp/install.sh" src/xbox-test/uwph/host/out/reg/k32.msix src/xbox-test/uwph/host/out/reg/test.cer | tail -2
if ! put "$runs/k32.log" 2>/dev/null | grep -q 'put HTTP 2'; then bash "$dp/launch.sh" "$rid" "$full" | tail -1; sleep 8; bash "$dp/kill.sh" "$full" >/dev/null || true; fi
check() { bash "$dp/wait-log.sh" "$full" k32.log 'REG RESULT' 120 "$runs/$2" >/dev/null; python3 src/xbox-test/uwph/host/check-reg.py "$runs/$2" "$1" | tee -a "$runs/summary.txt"; grep -h 'REGB\|REG-OK' "$runs/$2" | sort -u | head -6 | tee -a "$runs/summary.txt"; }
: > "$runs/summary.txt"
echo "== run1: fresh (registry.reset + empty registry.json): disposition created" | tee -a "$runs/summary.txt"
printf '{"format":"uwph-registry/1","keys":[]}\n' > "$runs/registry.json"; put "$runs/registry.json"; mark registry.reset
launch; check created console-run1.log
echo "== run2: relaunch, same sqlite db: disposition existing (persisted)" | tee -a "$runs/summary.txt"
launch; check existing console-run2.log
echo "== run3: one-time import of legacy registry.json (seed $(basename "$seed")) after registry.reset" | tee -a "$runs/summary.txt"
cp "$seed" "$runs/registry.json"; put "$runs/registry.json"; mark registry.reset
launch; check existing console-run3-import.log
grep -q 'REGB imported registry.json' "$runs/console-run3-import.log" && echo "IMPORT OK" | tee -a "$runs/summary.txt" || { echo "IMPORT NOT LOGGED" | tee -a "$runs/summary.txt"; exit 1; }
launch; check existing console-run4-after-import.log
echo "== kill-mid-write x$cycles (stress writer killed with the package terminated, then verify)" | tee -a "$runs/summary.txt"
for i in $(seq 1 "$cycles"); do
  mark regstress.txt 0; launch
  bash "$dp/wait-log.sh" "$full" k32.log "REGSTRESS writing i=[0-9]+ " 90 "$runs/stress-$i.log" >/dev/null || true
  sleep $(( (i % 3) + 1 )); bash "$dp/kill.sh" "$full" >/dev/null || true; sleep 2
  mark regverify.txt; launch
  bash "$dp/wait-log.sh" "$full" k32.log 'REG VERIFY DONE' 120 "$runs/verify-$i.log" >/dev/null || true
  grep -h 'REGSTRESS-VERIFY\|REG VERIFY DONE\|VIOLATION' "$runs/verify-$i.log" | tee -a "$runs/summary.txt"
done
bash "$dp/kill.sh" "$full" >/dev/null || true
grep -c 'REG VERIFY DONE ok' "$runs"/verify-*.log | tee -a "$runs/summary.txt"
