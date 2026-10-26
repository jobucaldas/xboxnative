#!/usr/bin/env bash
# Isolated Cs32 transaction. Never targets launcher/Uwph identities.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
runs=/workspace/x64-run/runs/cs32
mkdir -p "$runs"
exec 9>/workspace/x64-run/CONSOLE.lock
flock -w 600 9
export DP_LOCK_FD=9
full=Joao.XboxSteam.Cs32_0.1.1.0_x64__dk715arakrne0
rid='Joao.XboxSteam.Cs32_dk715arakrne0!App'
dp=xbox-test/dp
bash "$dp/install.sh" xbox-test/uwph/host/out/cs32/k32.msix xbox-test/uwph/host/out/cs32/test.cer
bash "$dp/launch.sh" "$rid" "$full"
bash "$dp/wait-log.sh" "$full" k32.log 'CS32 DONE|CS32 RESULT' 60 "$runs/console.log"
bash "$dp/kill.sh" "$full"
# Preserve the console log before removing the throwaway package.
bash "$dp/uninstall.sh" "$full"
