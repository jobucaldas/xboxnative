#!/usr/bin/env bash
# Fresh execution of ONLY Reg (Xboxnative.Reg): install -> launch -> pull log -> check. Holds the console lock.
# REG_VERSION must match AppxManifest.Reg.xml. REG_INSTALL=0 skips (re)install.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../../.." && pwd)
cd "$root"
dp="$root/src/xbox-test/dp"
runs=${REG_RUNS:-/workspace/x64-run/runs/reg}
ver=$(sed -n 's/.*Name="Xboxnative.Reg".*Version="\([0-9.]*\)".*/\1/p' src/xbox-test/uwph/host/AppxManifest.Reg.xml)
full=Xboxnative.Reg_${ver}_x64__0bdeywx3x8szp
rid='Xboxnative.Reg_0bdeywx3x8szp!App'
mkdir -p "$runs"
exec 9>/workspace/x64-run/CONSOLE.lock
flock -w 600 9
export DP_LOCK_FD=9
if [[ ${REG_INSTALL:-1} == 1 ]]; then
    bash "$dp/install.sh" src/xbox-test/uwph/host/out/reg/k32.msix src/xbox-test/uwph/host/out/reg/test.cer
fi
launch_and_check() {  # $1 = expected disposition of RegCreateKeyExW, $2 = console log name
    bash "$dp/kill.sh" "$full" || true
    printf 'REG awaiting fresh activation\n' > "$runs/k32.log"
    bash "$dp/put-file.sh" "$full" '\LocalState' "$runs/k32.log"
    bash "$dp/launch.sh" "$rid" "$full"
    bash "$dp/wait-log.sh" "$full" k32.log 'REG RESULT' 120 "$runs/$2"
    python3 src/xbox-test/uwph/host/check-reg.py "$runs/$2" "$1"
}
printf 'REG awaiting fresh activation\n' > "$runs/k32.log"
# Run 1: reset registry.json so the key is created fresh. First launch of a new install has no
# LocalState yet, so launch once, stop, then reset (put fails with HTTP 500 until it exists).
if ! bash "$dp/put-file.sh" "$full" '\LocalState' "$runs/k32.log" 2>/dev/null | grep -q 'put HTTP 2'; then
    bash "$dp/launch.sh" "$rid" "$full"; sleep 8; bash "$dp/kill.sh" "$full" || true
fi
printf '{"format":"uwph-registry/1","keys":[]}\n' > "$runs/registry.json"
bash "$dp/put-file.sh" "$full" '\LocalState' "$runs/registry.json"
launch_and_check created console.log
bash "$dp/get-file.sh" "$full" '\LocalState' registry.json "$runs/registry-after-run1.json"
# Run 2: new process, same registry.json: key must be opened (not created) and all calls still pass.
launch_and_check existing console-relaunch.log
bash "$dp/get-file.sh" "$full" '\LocalState' registry.json "$runs/registry-after-run2.json"
bash "$dp/kill.sh" "$full" || true
