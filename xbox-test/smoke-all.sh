#!/usr/bin/env bash
# Off-device Wine/static checks plus a fresh run of the installed Xbox test app.
# No build/install or identity changes. The 0.1.24 package contains k32+ws AND d3dt.
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
offline=0
case "${1:-}" in
    --offline) offline=1 ;;
    '') ;;
    *) echo "usage: $0 [--offline]" >&2; exit 2 ;;
esac
if (( $# > 1 )); then echo "usage: $0 [--offline]" >&2; exit 2; fi
runs=${SMOKE_RUNS:-/workspace/x64-run/runs}
mkdir -p "$runs"
log="$runs/smoke-$(date +%F-%H%M).log"
# Preserve multiple invocations in the same minute, including offline runs.
exec > >(tee -a "$log") 2>&1
artifacts=$(mktemp -d "$runs/smoke-evidence-$(date +%F-%H%M%S)-XXXXXX")
echo "=== smoke $(date -Is) offline=$offline ==="
echo "Log: $log"
echo "Evidence: $artifacts"
passed=0; failed=0
section() {
    local name=$1; shift
    echo "=== $name ==="
    # A subshell with errexit preserves failures inside functions. Do not put
    # the function itself in an if/|| context (Bash would disable errexit).
    set +e
    ( set -e; "$@" )
    local rc=$?
    set -e
    if (( rc == 0 )); then
        echo "PASS: $name"; passed=$((passed + 1))
    else
        echo "FAIL: $name (exit $rc)"; failed=$((failed + 1))
    fi
}
dispatch() {
    bash xbox-test/uwph/spike-dispatch/run.sh | tee "$artifacts/dispatch.log"
    grep -Fx 'run.sh: ALL OK' "$artifacts/dispatch.log"
}
imports() {
    python3 tools/xbox/check_import_map.py /workspace/megabonk-from-console | tee "$artifacts/import-map.log"
    grep -E '^0 unclassified DLLs .*4/4 binaries scanned\)$' "$artifacts/import-map.log"
}
section 'dispatch (off-device Wine)' dispatch
section 'import map (off-device static inspection)' imports

console_sections=(
    'console fresh launch (Xbox Series S)'
    'k32+ws (console; 71 checks)'
    'd3dt (console; 300 frames hr=0)'
    'wxl (console; mapped PE, W^X)'
    'hid (console; 20 symbols, 23 checks)'
    'console cleanup (test identity only)'
    'TLS+unwind (console; two threads and cross-image catch)'
)
# Every console section counts as a failure (never a skip) when its
# prerequisite is broken: print one line per section and bump the counters.
fail_console_sections() {
    local reason=$1 name
    for name in "${console_sections[@]}"; do
        echo "=== $name ==="
        echo "FAIL: $reason"
        failed=$((failed + 1))
    done
}
host_dir="$root/xbox-test/uwph/host"
build_commit() {
    local c; c=$(git -C "$root" rev-parse --short=12 HEAD)
    if [[ -n $(git -C "$root" status --porcelain --untracked-files=no) ]]; then c="$c-dirty"; fi
    printf '%s' "$c"
}
# Rebuild when out/layout is missing or older than any tracked host source.
# The stamp (written after a successful build) is the reference time; without
# one, fall back to the layout directory's own mtime.
host_build_needed() {
    local layout="$host_dir/out/layout" stamp="$host_dir/out/layout/.smoke-built" ref f
    if [[ ! -d $layout ]]; then echo "out/layout missing"; return 0; fi
    ref=$layout; [[ -f $stamp ]] && ref=$stamp
    while IFS= read -r f; do
        case $f in *.log|*.png|*.md) continue ;; esac
        if [[ $root/$f -nt $ref ]]; then echo "stale: $f newer than $(basename "$ref")"; return 0; fi
    done < <(git -C "$root" ls-files xbox-test/uwph/host xbox-test/hid xbox-test/shared)
    return 1
}
host_build() {
    local why
    if why=$(host_build_needed); then
        echo "build guard: rebuilding host layout via build.sh ($why)"
        # build.sh creates out/test.pfx only when missing, so an existing one is reused.
        bash "$host_dir/build.sh"
        touch "$host_dir/out/layout/.smoke-built"
        echo "build guard: build OK"
    else
        echo "build guard: out/layout up to date, no rebuild"
    fi
    echo "build commit: $(build_commit)"
}
echo "smoke build commit: $(build_commit)"

# Probe Device Portal through the tailnet SOCKS proxy (no credentials needed:
# any HTTP status, even 401, means the console answered).
console_probe() {
    local code
    code=$(flock /workspace/x64-run/CONSOLE.lock curl -sk -m 20 --socks5-hostname "${DP_SOCKS:-localhost:1055}" -o /dev/null -w '%{http_code}' \
        "https://${DP_HOST:-192.168.15.96}:11443/" 2>/dev/null) || true
    [[ $code =~ ^[1-5][0-9][0-9]$ ]]
}
build_rc=0
if (( ! offline )); then
    echo '=== host build guard ==='
    # Plain statements (not an if/|| context) so errexit applies inside host_build.
    set +e
    ( set -e; host_build )
    build_rc=$?
    set -e
fi
if (( offline )); then
    echo 'SKIP: k32+ws (console; --offline)'
    echo 'SKIP: d3dt (console; --offline)'
    echo 'SKIP: TLS+unwind (console; --offline)'
elif (( build_rc != 0 )); then
    echo "build commit (failed build): $(build_commit)"
    fail_console_sections 'host build failed'
elif ! console_probe; then
    fail_console_sections 'console unreachable (network)'
else
    dp="$root/xbox-test/dp"
    rid='Joao.XboxSteam.Uwph_dk715arakrne0!App'
    console_log="$artifacts/console.log"
    # Hold the actual shared lock across stop/clear/launch/read/check; helpers
    # reuse fd 9 through dp-lib.sh, including wait-log's child get-file calls.
    export DP_LOCK=/workspace/x64-run/CONSOLE.lock
    exec 9>"$DP_LOCK"
    flock -w 600 9
    export DP_LOCK_FD=9
    # Select the installed version while holding the lock: other lanes can
    # advance Uwph between runs. All existing assertions remain mandatory.
    full=${SMOKE_PACKAGE_FULL_NAME:-}
    if [[ -z $full ]]; then
        /workspace/x64-run/dp.sh /api/app/packagemanager/packages > "$artifacts/packages.json"
        full=$(python3 - "$artifacts/packages.json" <<'PY'
import json, re, sys
pattern = r'Joao\.XboxSteam\.Uwph_(\d+)\.(\d+)\.(\d+)\.(\d+)_x64__dk715arakrne0'
packages = []
for package in json.load(open(sys.argv[1])).get('InstalledPackages', []):
    name = package.get('PackageFullName', '')
    match = re.fullmatch(pattern, name)
    if match:
        packages.append((tuple(map(int, match.groups())), name))
if not packages:
    sys.exit('No installed Uwph smoke package')
print(max(packages)[1])
PY
        )
    fi
    [[ $full =~ ^Joao\.XboxSteam\.Uwph_[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+_x64__dk715arakrne0$ ]] || {
        echo 'Invalid smoke package identity'; exit 2;
    }
    echo "Console package: $full"
    stop_app() {
        local processes
        processes=$(bash "$dp/ps.sh")
        if grep -Fq "pkg=$full" <<< "$processes"; then
            bash "$dp/kill.sh" "$full"
        fi
        for _ in 1 2 3 4 5; do
            processes=$(bash "$dp/ps.sh")
            if ! grep -Fq "pkg=$full" <<< "$processes"; then return 0; fi
            sleep 2
        done
        echo 'Test package still running'; return 1
    }
    prepare_console() {
        stop_app
        # Destroy stale success evidence while stopped, then verify overwrite.
        printf 'SMOKE awaiting fresh activation %s\n' "$(date -Is)" > "$artifacts/k32.log"
        bash "$dp/put-file.sh" "$full" '\LocalState' "$artifacts/k32.log" | tee "$artifacts/put.log"
        grep -Eq '^put HTTP 2[0-9][0-9]$' "$artifacts/put.log"
        bash "$dp/get-file.sh" "$full" '\LocalState' k32.log "$artifacts/reset.log"
        cmp "$artifacts/k32.log" "$artifacts/reset.log"
        bash "$dp/put-file.sh" "$full" '\LocalState' xbox-test/uwph/host/out/uwph/layout/wxl-test.exe | tee "$artifacts/put-wxl.log"
        grep -Eq '^put HTTP 2[0-9][0-9]$' "$artifacts/put-wxl.log"
        bash "$dp/get-file.sh" "$full" '\LocalState' wxl-test.exe "$artifacts/wxl-test.exe"
        cmp xbox-test/uwph/host/out/uwph/layout/wxl-test.exe "$artifacts/wxl-test.exe"
        bash "$dp/launch.sh" "$rid" "$full" | tee "$artifacts/launch.log"
        grep -Eq '^launch HTTP 2[0-9][0-9]$' "$artifacts/launch.log"
        bash "$dp/wait-log.sh" "$full" k32.log 'D3DT RETURN|D3DT .*FAILED|K32 .*FAILED' 120 "$console_log"
        cat "$console_log"
        grep -Eq '\] boot: Initialize \(pid=[0-9]+\)$' "$console_log"
        ! grep -q 'SMOKE awaiting' "$console_log"
    }
    k32() {
        grep -Eq '\] checks=71 failures=0$' "$console_log"
        grep -Eq '\] K32 RESULT failures=0$' "$console_log"
        test "$(grep -cE '\] PASS ' "$console_log")" -eq 71
        if grep -Eq '\] .*FAIL' "$console_log"; then return 1; fi
    }
    d3dt() {
        for step in 'CreateVertexShader DXBC' 'CreatePixelShader DXBC' 'CreateInputLayout' 'CreateBuffer triangle'; do
            grep -F "] D3DT $step hr=0x00000000" "$console_log"
        done
        grep -F '] D3DT triangle pipeline ready: precompiled DXBC, Draw(3,0) each frame' "$console_log"
        grep -Eq '\] D3DT feature_level=0xb000 separate x64 PE$' "$console_log"
        grep -Eq '\] D3DT system CreateSwapChainForCoreWindow hr=0x00000000$' "$console_log"
        for frame in 60 120 180 240 300; do
            grep -Eq "\\] D3DT presented=$frame hr=0x00000000 elapsed=" "$console_log"
        done
        grep -Eq '\] D3DT RESULT presented=300 hr=0x00000000 final_clear=0.90,0.12,0.65,1.00$' "$console_log"
        grep -Eq '\] D3DT RETURN hr=0x00000000$' "$console_log"
        if grep -Eq '\] .*FAIL' "$console_log"; then return 1; fi
        if grep 'hr=' "$console_log" | grep -v 'hr=0x00000000'; then return 1; fi
    }
    wxl() {
        python3 xbox-test/uwph/host/check-wxl.py "$console_log"
    }
    hid() {
        grep -Eq '\] HID-STUB-OK symbols=20 checks=23$' "$console_log"
        grep -Eq '\] HID RESULT failures=0$' "$console_log"
        test "$(grep -cE '\] HID CHECK .* PASS$' "$console_log")" -eq 23
        if grep -Eq '\] .*FAIL' "$console_log"; then return 1; fi
    }
    section 'console fresh launch (Xbox Series S)' prepare_console
    section 'k32+ws (console; 71 checks)' k32
    section 'd3dt (console; 300 frames hr=0)' d3dt
    section 'wxl (console; mapped PE, W^X)' wxl
    section 'hid (console; 20 symbols, 23 checks)' hid
    section 'console cleanup (test identity only)' stop_app
    tls_console() {
    full=${SMOKE_TLS_PACKAGE_FULL_NAME:-}
    if [[ -z $full ]]; then
        /workspace/x64-run/dp.sh /api/app/packagemanager/packages > "$artifacts/packages.json"
        full=$(python3 - "$artifacts/packages.json" <<'PY'
import json, re, sys
pattern = r'Joao\.XboxSteam\.Tls_(\d+)\.(\d+)\.(\d+)\.(\d+)_x64__dk715arakrne0'
packages = []
for package in json.load(open(sys.argv[1])).get('InstalledPackages', []):
    name = package.get('PackageFullName', '')
    match = re.fullmatch(pattern, name)
    if match:
        packages.append((tuple(map(int, match.groups())), name))
if not packages:
    sys.exit('No installed Tls smoke package')
print(max(packages)[1])
PY
        )
    fi
    [[ $full =~ ^Joao\.XboxSteam\.Tls_[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+_x64__dk715arakrne0$ ]] || {
        echo 'Invalid smoke package identity'; exit 2;
    }
    echo "Console package: $full"
        rid='Joao.XboxSteam.Tls_dk715arakrne0!App'
        console_log="$artifacts/tls-console.log"
        trap stop_app EXIT
        stop_app
        printf 'SMOKE awaiting fresh activation\n' > "$artifacts/k32.log"
        bash "$dp/put-file.sh" "$full" '\LocalState' "$artifacts/k32.log"
        bash "$dp/get-file.sh" "$full" '\LocalState' k32.log "$artifacts/tls-reset.log"
        cmp "$artifacts/k32.log" "$artifacts/tls-reset.log"
        for guest in wxl-test.exe lifecycle.dll tls-test.exe tls-test.dll; do
            source="xbox-test/uwph/host/out/tls/layout/$guest"
            bash "$dp/put-file.sh" "$full" '\LocalState' "$source"
            bash "$dp/get-file.sh" "$full" '\LocalState' "$guest" "$artifacts/tls-$guest"
            cmp "$source" "$artifacts/tls-$guest"
        done
        bash "$dp/launch.sh" "$rid" "$full"
        bash "$dp/wait-log.sh" "$full" k32.log 'D3DT RETURN|D3DT .*FAILED|K32 .*FAILED' 120 "$console_log"
        grep -Eq '\] boot: Initialize \(pid=[0-9]+\)$' "$console_log"
        ! grep -q 'SMOKE awaiting' "$console_log"
        python3 xbox-test/uwph/host/check-tls.py "$console_log"
        stop_app
    }
    section 'TLS+unwind (console; two threads and cross-image catch)' tls_console
    unset DP_LOCK_FD
    flock -u 9
    exec 9>&-
fi
echo "=== SUMMARY: $passed passed, $failed failed; log=$log ==="
(( failed == 0 ))
