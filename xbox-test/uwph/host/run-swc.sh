#!/usr/bin/env bash
# Fresh execution of ONLY Swc; helpers inherit the shared lock.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
dp="$root/xbox-test/dp"
runs=/workspace/x64-run/runs/swc
full=Joao.XboxSteam.Swc_0.1.0.0_x64__dk715arakrne0
rid='Joao.XboxSteam.Swc_dk715arakrne0!App'
mkdir -p "$runs"
exec 9>/workspace/x64-run/CONSOLE.lock
flock -w 600 9
export DP_LOCK_FD=9
if [[ ${SWC_INSTALL:-0} == 1 ]]; then
    bash "$dp/install.sh" xbox-test/uwph/host/out/swc/k32.msix xbox-test/uwph/host/out/swc/test.cer
    bash "$dp/launch.sh" "$rid" "$full"
    sleep 3
fi
bash "$dp/kill.sh" "$full"
bash "$dp/put-file.sh" "$full" '\LocalState' xbox-test/uwph/host/out/swc/layout/swc-test.exe | tee "$runs/put.log"
grep -Eq '^put HTTP 2[0-9][0-9]$' "$runs/put.log"
bash "$dp/get-file.sh" "$full" '\LocalState' swc-test.exe "$runs/swc-test.exe"
cmp xbox-test/uwph/host/out/swc/layout/swc-test.exe "$runs/swc-test.exe"
printf 'SWC awaiting fresh activation\n' > "$runs/k32.log"
bash "$dp/put-file.sh" "$full" '\LocalState' "$runs/k32.log"
bash "$dp/get-file.sh" "$full" '\LocalState' k32.log "$runs/reset.log"
cmp "$runs/k32.log" "$runs/reset.log"
bash "$dp/launch.sh" "$rid" "$full"
bash "$dp/wait-log.sh" "$full" k32.log 'SWC RESULT|SWC .*FAIL|WXL FAIL' 120 "$runs/console.log"
python3 xbox-test/uwph/host/check-swc.py "$runs/console.log"
bash "$dp/screenshot.sh" "$runs/swc.png"
# Keep visible for inspection; smoke or next invocation may stop this identity.
