#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Load a Valve client verified by steam_client_package.h and probe its host API
# under Wine. This deliberately stops before account-store authentication.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
env_script=/tmp/xboxnative-cookie-integration-20261104/env.sh
if [[ $# != 1 || ! -d "$1" ]]; then
    printf 'usage: %s <directory-with-verified-client-runtime-dlls>\n' "$0" >&2
    exit 2
fi
if [[ -n "${STEAM_REFRESH_TOKEN:-}" ]]; then
    printf '%s\n' 'BLOCKED: refresh-token environment is present, but no validated Valve account-store writer is implemented; token was not passed to Wine.' >&2
    exit 11
fi
client_dir="$(realpath "$1")"
out="$(mktemp -d /tmp/steam-host-probe.XXXXXX)"
trap 'rm -rf "$out"' EXIT
prefix="/tmp/xbn-genuine-steamclient-wine"
mkdir -p "$prefix"
cc="${CC:-x86_64-w64-mingw32-gcc}"
bash "$env_script" env "$cc" -x c -Wall -Wextra -Werror -municode -c -o "$out/steam_host_probe.o" "$here/steam_host_probe.cpp"
crt2u="$(bash "$env_script" env "$cc" -print-file-name=crt2u.o)"
crtbegin="$(bash "$env_script" env "$cc" -print-file-name=crtbegin.o)"
crtend="$(bash "$env_script" env "$cc" -print-file-name=crtend.o)"
bash "$env_script" env "$cc" -nostdlib -municode -Wl,--subsystem,console -o "$out/steam_host_probe.exe" \
    "$crt2u" "$crtbegin" "$out/steam_host_probe.o" \
    -lmingw32 -lgcc -lgcc_eh -lmingwex -lmsvcrt -lkernel32 -ladvapi32 -lshell32 -luser32 -lkernel32 -lntdll "$crtend"
cd "$out"
set +e
bash "$env_script" env PRUN_WINEPREFIX="$prefix" WINEPREFIX="$prefix" env -u STEAM_REFRESH_TOKEN wine "$out/steam_host_probe.exe" "$client_dir"
status=$?
set -e
if [[ $status == 10 ]]; then
    printf '%s\n' 'STEAM-HOST-PROBE STOP: authenticated account-store data is the next required input.'
    exit 0
fi
exit "$status"
