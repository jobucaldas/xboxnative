#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Load a Valve client verified by steam_client_package.h and probe its host API
# under Wine. Optional account inputs create a temporary GameNative-format store.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
env_script=/tmp/xboxnative-cookie-integration-20261104/env.sh
if [[ $# -lt 1 || ! -d "$1" ]]; then
    printf 'usage: %s <directory-with-verified-client-runtime-dlls> [--token-file <path> --login <name> --steamid <id>]\n' "$0" >&2
    exit 2
fi
client_dir="$(realpath "$1")"
shift
probe_args=()
if [[ $# != 0 ]]; then
    if [[ $# != 6 || "$1" != --token-file || "$3" != --login || "$5" != --steamid || ! -f "$2" ]]; then
        printf 'usage: %s <directory-with-verified-client-runtime-dlls> [--token-file <path> --login <name> --steamid <id>]\n' "$0" >&2
        exit 2
    fi
    token_path="$(realpath "$2")"
    token_win_path="$(bash "$env_script" env winepath -w "$token_path")"
    probe_args=(--token-file "$token_win_path" --login "$4" --steamid "$6")
fi
out="$(mktemp -d /tmp/steam-host-probe.XXXXXX)"
keep="${STEAM_HOST_PROBE_KEEP:-0}"
cleanup() {
    if [[ "$keep" == 1 ]]; then
        printf 'STEAM-HOST-PROBE files retained in %s\n' "$out"
    else
        rm -rf "$out"
    fi
}
trap cleanup EXIT
prefix="${STEAM_HOST_PROBE_WINEPREFIX:-/tmp/xbn-genuine-steamclient-login-wine}"
mkdir -p "$prefix"
cc="${CC:-x86_64-w64-mingw32-gcc}"
bash "$env_script" env "$cc" -x c++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -municode -c -o "$out/steam_host_probe.o" "$here/steam_host_probe.cpp"
crt2u="$(bash "$env_script" env "$cc" -print-file-name=crt2u.o)"
crtbegin="$(bash "$env_script" env "$cc" -print-file-name=crtbegin.o)"
crtend="$(bash "$env_script" env "$cc" -print-file-name=crtend.o)"
bash "$env_script" env "$cc" -nostdlib -municode -Wl,--subsystem,console -o "$out/steam_host_probe.exe" \
    "$crt2u" "$crtbegin" "$out/steam_host_probe.o" \
    -lmingw32 -lgcc -lgcc_eh -lmingwex -lmsvcrt -lkernel32 -ladvapi32 -lshell32 -luser32 -lkernel32 -lntdll "$crtend"
cd "$out"
set +e
bash "$env_script" env PRUN_WINEPREFIX="$prefix" WINEPREFIX="$prefix" WINEDEBUG="${WINEDEBUG:-}" env -u STEAM_REFRESH_TOKEN wine "$out/steam_host_probe.exe" "$client_dir" "${probe_args[@]}" 2>"$out/wine.trace"
status=$?
set -e
if [[ $status == 0 ]]; then
    printf '%s\n' 'STEAM-HOST-PROBE completed; inspect logs/ for Valve client diagnostics.'
fi
exit "$status"
