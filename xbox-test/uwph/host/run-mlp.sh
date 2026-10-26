#!/usr/bin/env bash
# Install ONLY the isolated Mlp identity; upload immutable game inputs, fresh run.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
dp="$root/xbox-test/dp"
runs=${MLP_RUNS:-/workspace/x64-run/runs/mlp}
full=Joao.XboxSteam.Mlp_0.1.32.0_x64__dk715arakrne0
rid='Joao.XboxSteam.Mlp_dk715arakrne0!App'
mkdir -p "$runs"
exec 9>/workspace/x64-run/CONSOLE.lock
flock -w "${MLP_LOCK_WAIT:-600}" 9
export DP_LOCK_FD=9
export DP_TIMEOUT=${DP_TIMEOUT:-600}
if [[ ${MLP_REPLACE:-0} == 1 ]]; then
    # Rebuilds keep the requested version. Replace ONLY this lane's identity.
    bash "$dp/kill.sh" "$full"
    bash "$dp/uninstall.sh" "$full"
    sleep 3
fi
/workspace/x64-run/dp.sh /ext/user > "$runs/user.json"
if [[ ${MLP_SKIP_INSTALL:-0} != 1 ]]; then
    bash "$dp/install.sh" xbox-test/uwph/host/out/mlp/k32.msix xbox-test/uwph/host/out/mlp/test.cer
fi
# First activation establishes LocalState; game files do not yet exist.
bash "$dp/launch.sh" "$rid" "$full"
sleep 4
bash "$dp/kill.sh" "$full"
# Only these three inputs are mapped; GameAssembly is inspected off-device.
if [[ ${MLP_SKIP_UPLOAD:-0} != 1 ]]; then
for binary in Megabonk.exe UnityPlayer.dll baselib.dll; do
    uploaded=0
    for attempt in 1 2 3 4; do
        if bash "$dp/put-file.sh" "$full" '\LocalState' "/workspace/megabonk-from-console/$binary" | tee "$runs/put-$binary.log" &&
           grep -Eq '^put HTTP 2[0-9][0-9]$' "$runs/put-$binary.log"; then
            uploaded=1; break
        fi
        echo "Upload retry $attempt for $binary"
        sleep 3
    done
    [[ $uploaded == 1 ]]
done
fi
printf 'MLP awaiting fresh activation\n' > "$runs/k32.log"
bash "$dp/put-file.sh" "$full" '\LocalState' "$runs/k32.log"
rm -f "$runs/reset.log"
bash "$dp/get-file.sh" "$full" '\LocalState' k32.log "$runs/reset.log"
cmp "$runs/k32.log" "$runs/reset.log"
bash "$dp/launch.sh" "$rid" "$full"
bash "$dp/wait-log.sh" "$full" k32.log 'MLP-PROBE-DONE|MLP-PROBE-FAILED' 120 "$runs/console-mlp.log"
python3 xbox-test/uwph/host/check-mlp.py "$runs/console-mlp.log" /workspace/megabonk-from-console
bash "$dp/ps.sh" "$full" | tee "$runs/process.log"
grep -Fq "pkg=$full" "$runs/process.log"
# Leave running for acceptance observation; later smoke uses its own identity.
